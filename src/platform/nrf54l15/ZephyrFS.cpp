/*
 * ZephyrFS — LittleFS mount + init for nRF54L15.
 *
 * Uses the "storage" partition defined in the board DTS.
 * Mount point is "/littlefs". Meshtastic paths like "/prefs/nodes.proto"
 * become "/littlefs/prefs/nodes.proto".
 */

#include "ZephyrFS.h"
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>

/* LittleFS work area — Zephyr allocates internal caches here */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_data);

static struct fs_mount_t lfs_mnt = {
    .type = FS_LITTLEFS,
    .mnt_point = "/littlefs",
    .fs_data = &lfs_data,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
};

ZephyrFS zephyrFS;

bool ZephyrFS::begin()
{
    int rc = fs_mount(&lfs_mnt);
    if (rc == -ENODEV) {
        printk("LittleFS : mount failed (%d), partition may need format\n", rc);
        return false;
    }
    if (rc != 0) {
        printk("LittleFS : mount error (%d)\n", rc);
        return false;
    }

    printk("LittleFS : mounted at /littlefs\n");

    /* Create /prefs directory if it doesn't exist */
    struct fs_dirent entry;
    if (fs_stat("/littlefs/prefs", &entry) != 0) {
        rc = fs_mkdir("/littlefs/prefs");
        if (rc == 0 || rc == -EEXIST) {
            printk("LittleFS : /prefs directory ready\n");
        } else {
            printk("LittleFS : mkdir /prefs failed (%d)\n", rc);
        }
    }

    return true;
}
