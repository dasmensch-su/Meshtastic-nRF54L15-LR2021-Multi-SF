/*
 * bt_rand_stub.c — Fake bt_rand() for Renode testing
 *
 * CONFIG_BT_HOST_CRYPTO=n excludes crypto.c (which provides bt_rand).
 * The BLE broadcaster still calls bt_rand() for address generation.
 * This stub returns a fixed deterministic value — not cryptographically
 * secure, but sufficient for Renode simulation of BLE advertising.
 *
 * On real hardware: enable CONFIG_BT_HOST_CRYPTO=y with a working entropy
 * source (Phase 4 CRACEN workaround or real random seed).
 */

#include <zephyr/kernel.h>

/* Only provide bt_rand stub when controller crypto is disabled (simulation).
 * With CONFIG_BT_CTLR_CRYPTO=y (hardware), the real bt_rand is provided
 * by Zephyr's BLE controller crypto module. */
#if !IS_ENABLED(CONFIG_BT_CTLR_CRYPTO)

#include <stddef.h>
#include <string.h>

int bt_rand(void *buf, size_t len)
{
    /* Fill with fixed pattern — acceptable for Renode sim only */
    memset(buf, 0xAB, len);
    return 0;
}

#endif /* !CONFIG_BT_CTLR_CRYPTO */
