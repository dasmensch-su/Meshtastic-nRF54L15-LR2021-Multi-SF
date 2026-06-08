/*
 * ble_phone_api.cpp — Meshtastic BLE Phone API for nRF54L15 (Zephyr)
 *
 * Implements the BLE GATT service that the Meshtastic phone app connects to.
 * Uses Zephyr's native BT stack (not NimBLE) with the standard Meshtastic UUIDs:
 *
 *   Service: 6ba1b218-15a8-461f-9fa8-5dcae273eafd
 *   ToRadio:   f75c76d2-129e-4dad-a1dd-7866124401e7  (WRITE)
 *   FromRadio: 2c55e69e-4993-11ed-b878-0242ac120002  (READ)
 *   FromNum:   ed9da18c-a800-4f66-a670-aa7547e34453  (READ + NOTIFY)
 *
 * Compiled only when CONFIG_BT_PERIPHERAL=y (hardware builds).
 */

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_BT_PERIPHERAL)

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "PhoneAPI.h"
#include "NodeDB.h"
#include "main.h"

/* Forward declaration */
void ble_notify_fromnum(void);

/* Concrete PhoneAPI subclass — must implement pure virtuals */
class BlePhoneAPI : public PhoneAPI {
protected:
    /* Called when a new mesh packet is available for the phone.
     * Send BLE notification to wake the phone app so it reads FromRadio. */
    void onNowHasData(uint32_t fromRadioNum) override {
        ble_notify_fromnum();
    }

    /* Check if BLE client is still connected */
    bool checkIsConnected() override;
};

/* ---- Meshtastic BLE UUIDs ---- */

/* Macro form for use in advertising data */
#define MESH_SERVICE_UUID_VAL \
    BT_UUID_128_ENCODE(0x6ba1b218, 0x15a8, 0x461f, 0x9fa8, 0x5dcae273eafd)

static const struct bt_uuid_128 mesh_service_uuid =
    BT_UUID_INIT_128(MESH_SERVICE_UUID_VAL);

static const struct bt_uuid_128 toradio_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0xf75c76d2, 0x129e, 0x4dad, 0xa1dd, 0x7866124401e7));

static const struct bt_uuid_128 fromradio_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x2c55e69e, 0x4993, 0x11ed, 0xb878, 0x0242ac120002));

static const struct bt_uuid_128 fromnum_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0xed9da18c, 0xa800, 0x4f66, 0xa670, 0xaa7547e34453));

static const struct bt_uuid_128 logradio_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x5a3d6e49, 0x06e6, 0x4423, 0x9944, 0xe9de8cdf9547));

/* ---- State ---- */

static PhoneAPI *phoneApi;
static struct bt_conn *current_conn;

bool BlePhoneAPI::checkIsConnected() { return current_conn != nullptr; }
static bool fromnum_notify_enabled;
static bool logradio_notify_enabled;
static uint32_t fromnum_value;  /* Sequence counter for FromNum notifications */

/* Buffers for BLE characteristic data */
static uint8_t fromradio_buf[MAX_TO_FROM_RADIO_SIZE];
static size_t fromradio_len;

/* ---- GATT Callbacks ---- */

/* ToRadio WRITE — phone sends protobuf commands/packets to device */
static ssize_t toradio_write_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags)
{
    if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
        return 0;
    }

    if (!phoneApi) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    phoneApi->handleToRadio((const uint8_t *)buf, len);

    return len;
}

/* FromRadio READ — phone polls for protobuf responses from device */
static ssize_t fromradio_read_cb(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 void *buf, uint16_t len, uint16_t offset)
{
    if (!phoneApi) {
        return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
    }

    /* Only fetch new data on first read (offset == 0) */
    if (offset == 0) {
        if (phoneApi->available()) {
            fromradio_len = phoneApi->getFromRadio(fromradio_buf);
        } else {
            fromradio_len = 0;
        }
    }

    /* When no data, return 0 length — app should stop reading until
     * the next FromNum notification signals new data is available. */
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             fromradio_buf, fromradio_len);
}

/* FromNum READ — returns the 4-byte notification counter */
static ssize_t fromnum_read_cb(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             &fromnum_value, sizeof(fromnum_value));
}

/* FromNum CCC changed — phone enables/disables notifications */
static void fromnum_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    fromnum_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("BLE      : FromNum notifications %s\n",
           fromnum_notify_enabled ? "enabled" : "disabled");
}

/* LogRadio CCC changed — phone enables/disables log notifications */
static void logradio_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    logradio_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("BLE      : LogRadio notifications %s\n",
           logradio_notify_enabled ? "enabled" : "disabled");
}

/* ---- GATT Service Definition ---- */

BT_GATT_SERVICE_DEFINE(meshtastic_svc,
    /* Primary Service */
    BT_GATT_PRIMARY_SERVICE(&mesh_service_uuid),

    /* ToRadio: WRITE (phone → device) */
    BT_GATT_CHARACTERISTIC(&toradio_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, toradio_write_cb, NULL),

    /* FromRadio: READ (device → phone) */
    BT_GATT_CHARACTERISTIC(&fromradio_uuid.uuid,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           fromradio_read_cb, NULL, NULL),

    /* FromNum: READ + NOTIFY (notification counter) */
    BT_GATT_CHARACTERISTIC(&fromnum_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           fromnum_read_cb, NULL, NULL),
    BT_GATT_CCC(fromnum_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    /* LogRadio: NOTIFY (device log stream to phone) */
    BT_GATT_CHARACTERISTIC(&logradio_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(logradio_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* ---- Connection Callbacks ---- */

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("BLE      : Connection failed (err 0x%02x)\n", err);
        return;
    }

    printk("BLE      : Phone connected\n");
    current_conn = bt_conn_ref(conn);

    /* Reset PhoneAPI state for new connection */
    if (phoneApi) {
        phoneApi->close();
    }
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
    printk("BLE      : Phone disconnected (reason 0x%02x)\n", reason);

    if (phoneApi) {
        phoneApi->close();
    }

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    /* Restart advertising — use a work item to defer so BLE stack
     * fully releases connection resources before we restart */
    extern void ble_schedule_readvertise(void);
    ble_schedule_readvertise();
}

BT_CONN_CB_DEFINE(mesh_conn_callbacks) = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
};

/* ---- MTU Callback ---- */

static void mtu_updated_cb(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    printk("BLE      : MTU updated TX=%u RX=%u\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {
    .att_mtu_updated = mtu_updated_cb,
};

/* ---- Advertising ---- */

/* Connectable advertising with Meshtastic service UUID in primary ad
 * (scanners filter on UUID in the primary ad, name goes in scan response) */
static const struct bt_data adv_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, MESH_SERVICE_UUID_VAL),
};

static char ble_adv_name[32];

static struct bt_data scan_resp[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, ble_adv_name, 0),
};

void ble_start_advertising(void)
{
    const char *devName = getDeviceName();
    size_t len = strlen(devName);
    if (len >= sizeof(ble_adv_name))
        len = sizeof(ble_adv_name) - 1;
    memcpy(ble_adv_name, devName, len);
    ble_adv_name[len] = '\0';
    scan_resp[0].data_len = len;

    bt_set_name(ble_adv_name);

    int err = bt_le_adv_start(BT_LE_ADV_CONN,
                              adv_data, ARRAY_SIZE(adv_data),
                              scan_resp, ARRAY_SIZE(scan_resp));
    if (err) {
        printk("BLE      : Advertising start failed (err %d)\n", err);
    } else {
        printk("BLE      : Connectable advertising as '%s'\n", ble_adv_name);
    }
}

/* Deferred re-advertise — called from disconnect callback via work queue
 * to give the BLE controller time to release connection resources */
static void readvertise_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(readvertise_work, readvertise_work_handler);

static void readvertise_work_handler(struct k_work *work)
{
    int err = bt_le_adv_stop();
    /* -EALREADY means advertising was already stopped (normal after disconnect) */
    if (err && err != -EALREADY) {
        printk("BLE      : adv_stop before re-advertise returned %d\n", err);
    }
    ble_start_advertising();
}

void ble_schedule_readvertise(void)
{
    k_work_schedule(&readvertise_work, K_MSEC(1000));
}

/* ---- Notification helper ---- */

/* Call this when new data is available for the phone (e.g., received mesh packet) */
void ble_notify_fromnum(void)
{
    if (!current_conn || !fromnum_notify_enabled) return;

    fromnum_value++;

    const struct bt_gatt_attr *attr =
        bt_gatt_find_by_uuid(meshtastic_svc.attrs, meshtastic_svc.attr_count,
                             &fromnum_uuid.uuid);
    if (attr) {
        bt_gatt_notify(current_conn, attr, &fromnum_value, sizeof(fromnum_value));
    }
}

/* ---- Initialization ---- */

/* Expose init result for debug dump */
int ble_init_result = -999;

extern "C" int ble_phone_api_init(void)
{
    printk("BLE      : Initializing Phone API...\n");

    int err = bt_enable(NULL);
    if (err) {
        printk("BLE      : bt_enable failed (%d)\n", err);
        ble_init_result = err;
        return err;
    }

    bt_gatt_cb_register(&gatt_callbacks);

    /* Create PhoneAPI instance (concrete subclass with onNotify) */
    phoneApi = new BlePhoneAPI();
    printk("BLE      : PhoneAPI created\n");

    /* Start connectable advertising */
    ble_start_advertising();

    printk("BLE      : Phone API ready (service UUID 6ba1b218...)\n");
    ble_init_result = 0;
    return 0;
}

#else /* !CONFIG_BT_PERIPHERAL */

int ble_init_result = 0;

extern "C" int ble_phone_api_init(void)
{
    return 0; /* no-op when BLE peripheral not configured */
}

#endif /* CONFIG_BT_PERIPHERAL */
