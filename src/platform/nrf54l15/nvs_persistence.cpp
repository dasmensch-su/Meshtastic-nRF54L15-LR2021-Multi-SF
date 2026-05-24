/*
 * nvs_persistence.cpp — Zephyr NVS-backed persistence for Meshtastic nRF54L15
 *
 * Maps Meshtastic protobuf config files to NVS key-value entries.
 * Replaces the FSCommon/LittleFS filesystem on platforms without one.
 */
#ifdef ARCH_NRF54L15

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "pb_encode.h"
#include "pb_decode.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"

/* NVS partition from DTS */
#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

static struct nvs_fs nvs;
static bool nvs_ready;

/* NVS ID mapping — each Meshtastic config file gets a unique 16-bit ID */
#define NVS_ID_NODEDB       1
#define NVS_ID_DEVICESTATE  2
#define NVS_ID_CONFIG       3
#define NVS_ID_MODULECONFIG 4
#define NVS_ID_CHANNELS     5
#define NVS_ID_UICONFIG     6
#define NVS_ID_TXHISTORY    7

/* Map filename to NVS ID.
 * Order matters: check more specific substrings before general ones
 * (e.g., "uiconfig" before "config", "module" before "config"). */
static uint16_t filename_to_nvs_id(const char *filename)
{
    if (!filename) return 0;
    if (strstr(filename, "nodes"))    return NVS_ID_NODEDB;
    if (strstr(filename, "device"))   return NVS_ID_DEVICESTATE;
    if (strstr(filename, "uiconfig")) return NVS_ID_UICONFIG;
    if (strstr(filename, "module"))   return NVS_ID_MODULECONFIG;
    if (strstr(filename, "channel"))  return NVS_ID_CHANNELS;
    if (strstr(filename, "config"))   return NVS_ID_CONFIG;
    if (strstr(filename, "transmit")) return NVS_ID_TXHISTORY;
    return 0;
}

/* Scratch buffer for protobuf encode/decode.
 * Zephyr NVS rejects reads when len > sector_size - 2*aligned_ate_size
 * and writes when len > sector_size - 4*aligned_ate_size.
 * nRF54L15 RRAM: sector=4096, write-block=16, aligned ATE=16 → max write=4032.
 * Must stay ≤ 4032 or nvs_read/nvs_write return -EINVAL. */
#define NVS_PROTO_BUF_SIZE 4000
static uint8_t nvs_proto_buf[NVS_PROTO_BUF_SIZE];

extern "C" {

int nvs_persistence_init(void)
{
    int rc;
    struct flash_pages_info info;

    nvs.flash_device = NVS_PARTITION_DEVICE;
    if (!device_is_ready(nvs.flash_device)) {
        printk("NVS      : flash device not ready\n");
        return -ENODEV;
    }

    nvs.offset = NVS_PARTITION_OFFSET;
    rc = flash_get_page_info_by_offs(nvs.flash_device, nvs.offset, &info);
    if (rc) {
        printk("NVS      : can't get flash page info (%d)\n", rc);
        return rc;
    }

    nvs.sector_size = info.size;
    /* Use all available sectors from the 36KB partition */
    nvs.sector_count = 36 * 1024 / info.size;
    if (nvs.sector_count < 3) nvs.sector_count = 3;

    rc = nvs_mount(&nvs);
    if (rc) {
        printk("NVS      : mount failed (%d)\n", rc);
        return rc;
    }

    nvs_ready = true;

    /* Check for format version — if mismatch, clear stale data.
     * Increment NVS_FORMAT_VERSION when config format changes. */
#define NVS_FORMAT_VERSION_ID 100
#define NVS_FORMAT_VERSION    2  /* bump this to force NVS clear on next boot */
    uint32_t stored_ver = 0;
    ssize_t vrc = nvs_read(&nvs, NVS_FORMAT_VERSION_ID, &stored_ver, sizeof(stored_ver));
    if (vrc < 0 || stored_ver != NVS_FORMAT_VERSION) {
        printk("NVS      : format version mismatch (%u vs %u), clearing\n",
               (unsigned)stored_ver, NVS_FORMAT_VERSION);
        nvs_clear(&nvs);
        /* nvs_clear() sets fs->ready = false — must re-mount */
        rc = nvs_mount(&nvs);
        if (rc) {
            printk("NVS      : re-mount after clear failed (%d)\n", rc);
            nvs_ready = false;
            return rc;
        }
        stored_ver = NVS_FORMAT_VERSION;
        nvs_write(&nvs, NVS_FORMAT_VERSION_ID, &stored_ver, sizeof(stored_ver));
    }

    printk("NVS      : mounted (sector_size=%u, sectors=%u, free=%d)\n",
           nvs.sector_size, nvs.sector_count,
           (int)nvs_calc_free_space(&nvs));
    return 0;
}

bool nvs_persistence_is_ready(void)
{
    return nvs_ready;
}

/* Save a protobuf struct to NVS.
 * Returns true on success. */
bool nvs_save_proto(const char *filename, size_t protoSize,
                    const pb_msgdesc_t *fields, const void *src_struct)
{
    if (!nvs_ready) return false;

    uint16_t id = filename_to_nvs_id(filename);
    if (id == 0) {
        printk("NVS      : unknown file '%s'\n", filename);
        return false;
    }

    /* Encode protobuf to buffer */
    size_t buf_size = (protoSize < NVS_PROTO_BUF_SIZE) ? protoSize : NVS_PROTO_BUF_SIZE;
    pb_ostream_t stream = pb_ostream_from_buffer(nvs_proto_buf, buf_size);
    if (!pb_encode(&stream, fields, src_struct)) {
        printk("NVS      : encode failed for '%s'\n", filename);
        return false;
    }

    size_t written = stream.bytes_written;
    ssize_t rc = nvs_write(&nvs, id, nvs_proto_buf, written);
    if (rc < 0) {
        printk("NVS      : write failed for '%s' (%d)\n", filename, (int)rc);
        return false;
    }

    printk("NVS      : saved '%s' (%u bytes, id=%u)%s\n",
           filename, (unsigned)written, id,
           rc == 0 ? " [unchanged]" : "");
    return true;
}

/* Load a protobuf struct from NVS.
 * Returns 0 on success, negative on error. */
int nvs_load_proto(const char *filename, size_t protoSize, size_t objSize,
                   const pb_msgdesc_t *fields, void *dest_struct)
{
    if (!nvs_ready) return -ENODEV;

    uint16_t id = filename_to_nvs_id(filename);
    if (id == 0) return -EINVAL;

    ssize_t rc = nvs_read(&nvs, id, nvs_proto_buf,
                          (protoSize < NVS_PROTO_BUF_SIZE) ? protoSize : NVS_PROTO_BUF_SIZE);
    if (rc < 0) {
        /* -ENOENT = not found (first boot), other = real error */
        if (rc != -ENOENT) {
            printk("NVS      : read failed for '%s' (%d)\n", filename, (int)rc);
        }
        return (int)rc;
    }

    /* Decode protobuf from buffer */
    /* Don't zero NodeDatabase — it contains a vector */
    if (fields != &meshtastic_NodeDatabase_msg)
        memset(dest_struct, 0, objSize);

    pb_istream_t stream = pb_istream_from_buffer(nvs_proto_buf, (size_t)rc);
    if (!pb_decode(&stream, fields, dest_struct)) {
        printk("NVS      : decode failed for '%s'\n", filename);
        return -EILSEQ;
    }

    printk("NVS      : loaded '%s' (%d bytes, id=%u)\n",
           filename, (int)rc, id);
    return 0;
}

} /* extern "C" */

#endif /* ARCH_NRF54L15 */
