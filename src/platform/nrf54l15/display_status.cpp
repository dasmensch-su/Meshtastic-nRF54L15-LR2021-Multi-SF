/*
 * display_status.cpp — OLED status screen for nRF54L15 hardware
 *
 * Shows mesh node status on the SSD1306 128x64 OLED display on the
 * LoRa Plus expansion board. Uses Zephyr's Character Frame Buffer (CFB)
 * subsystem for text rendering.
 *
 * Display layout (font 10x16, 12 chars × 4 lines):
 *   Line 0: "MESH xxxx"       — banner + short node ID
 *   Line 1: "Ch:channelname"  — primary channel name
 *   Line 2: "SFxx BWxxx"      — LoRa spreading factor + bandwidth
 *   Line 3: "Pn:x U:xx% xxm" — peer count, airtime util, uptime
 *
 * Compiled only when CONFIG_DISPLAY=y (hardware builds only).
 */

#include <zephyr/kernel.h>
#include "configuration.h"

#if IS_ENABLED(CONFIG_DISPLAY) && IS_ENABLED(CONFIG_CHARACTER_FRAMEBUFFER) && !HAS_SCREEN

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <stdio.h>
#include <string.h>

/* Meshtastic headers — access mesh state globals */
#if IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK)
#include "NodeDB.h"
#include "MeshRadio.h"
#include "Channels.h"
#include "airtime.h"
#endif

/* Display device from DTS chosen node */
static const struct device *display_dev;
static bool display_ready;

/* Screen dimensions after CFB init */
static uint16_t screen_w, screen_h;
static uint8_t font_w, font_h;

/* Update interval */
#define DISPLAY_UPDATE_MS 2000

/* Line buffer — 128px / 10px = 12 chars max + null */
#define LINE_BUF_LEN 20

static void display_draw(void)
{
    char line[LINE_BUF_LEN];

    cfb_framebuffer_clear(display_dev, false);

    /* ---- Line 0: Banner + node ID ---- */
#if IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK)
    if (nodeDB) {
        uint32_t num = nodeDB->getNodeNum();
        snprintf(line, sizeof(line), "MESH %04X",
                 (unsigned)(num & 0xFFFF));
    } else {
        snprintf(line, sizeof(line), "MESH ----");
    }
#else
    snprintf(line, sizeof(line), "MESH boot");
#endif
    cfb_print(display_dev, line, 0, 0);

    /* ---- Line 1: Channel name ---- */
#if IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK)
    {
        const char *ch_name = channels.getName(channels.getPrimaryIndex());
        if (ch_name && ch_name[0]) {
            snprintf(line, sizeof(line), "Ch:%.9s", ch_name);
        } else {
            snprintf(line, sizeof(line), "Ch:default");
        }
    }
#else
    snprintf(line, sizeof(line), "Ch:---");
#endif
    cfb_print(display_dev, line, 0, font_h);

    /* ---- Line 2: LoRa config ---- */
#if IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK)
    {
        /* Use modemPresetToParams to get actual values (config.lora fields
         * are 0 when using presets, so we can't read them directly) */
        extern const RegionInfo *myRegion;
        bool wide = myRegion ? myRegion->wideLora : false;
        float dispBw = 0; uint8_t dispSf = 0, dispCr = 0;
        modemPresetToParams(
            meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
            wide, dispBw, dispSf, dispCr);
        if (wide)
            snprintf(line, sizeof(line), "2.4G %u/%u",
                     (unsigned)dispSf, (unsigned)dispBw);
        else
            snprintf(line, sizeof(line), "915 SF%u/%u",
                     (unsigned)dispSf, (unsigned)dispBw);
    }
#else
    snprintf(line, sizeof(line), "SF-- BW---");
#endif
    cfb_print(display_dev, line, 0, font_h * 2);

    /* ---- Line 3: Peers + utilization + uptime ---- */
#if IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK)
    {
        size_t peers = 0;
        if (nodeDB) {
            size_t n = nodeDB->getNumMeshNodes();
            /* Sanity check — uninitialized memory can return huge values */
            if (n > 0 && n < 1000) peers = n - 1;
        }

        float util = 0.0f;
        if (airTime) {
            util = airTime->channelUtilizationPercent();
        }

        /* Uptime in minutes */
        uint32_t uptime_min = (uint32_t)(k_uptime_get() / 60000);

        snprintf(line, sizeof(line), "P:%u U:%u%% %um",
                 (unsigned)peers, (unsigned)util, uptime_min);
    }
#else
    snprintf(line, sizeof(line), "P:- U:-%%");
#endif
    cfb_print(display_dev, line, 0, font_h * 3);

    cfb_framebuffer_finalize(display_dev);
}

/* Zephyr work item for periodic display updates */
static void display_work_handler(struct k_work *work);
static K_WORK_DEFINE(display_work, display_work_handler);

static void display_timer_handler(struct k_timer *timer);
static K_TIMER_DEFINE(display_timer, display_timer_handler, NULL);

static void display_work_handler(struct k_work *work)
{
    if (display_ready) {
        display_draw();
    }
}

static void display_timer_handler(struct k_timer *timer)
{
    k_work_submit(&display_work);
}

extern "C" int display_status_init(void)
{
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Display  : device not ready\n");
        return -ENODEV;
    }

    if (display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10) != 0) {
        if (display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO01) != 0) {
            printk("Display  : pixel format failed\n");
            return -ENOTSUP;
        }
    }

    if (cfb_framebuffer_init(display_dev)) {
        printk("Display  : CFB init failed\n");
        return -EIO;
    }

    cfb_framebuffer_invert(display_dev);
    display_blanking_off(display_dev);

    screen_w = cfb_get_display_parameter(display_dev, CFB_DISPLAY_WIDTH);
    screen_h = cfb_get_display_parameter(display_dev, CFB_DISPLAY_HEIGHT);

    /* Use smallest available font (index 0) for maximum text */
    cfb_framebuffer_set_font(display_dev, 0);
    cfb_get_font_size(display_dev, 0, &font_w, &font_h);

    printk("Display  : %ux%u, font %ux%u\n",
           screen_w, screen_h, font_w, font_h);

    cfb_framebuffer_clear(display_dev, true);

    /* Show boot message */
    cfb_print(display_dev, "Meshtastic", 0, 0);
    cfb_print(display_dev, "nRF54L15", 0, font_h);
    cfb_print(display_dev, "Booting...", 0, font_h * 2);
    cfb_framebuffer_finalize(display_dev);

    display_ready = true;

    /* Start periodic updates */
    k_timer_start(&display_timer, K_MSEC(DISPLAY_UPDATE_MS),
                  K_MSEC(DISPLAY_UPDATE_MS));

    printk("Display  : status screen started (update every %d ms)\n",
           DISPLAY_UPDATE_MS);
    return 0;
}

#else /* !CONFIG_DISPLAY || !CONFIG_CHARACTER_FRAMEBUFFER */

extern "C" int display_status_init(void)
{
    return 0; /* no-op when display is not configured */
}

#endif
