/*
 * DEBUG: Custom fatal error handler for nRF54L15.
 *
 * Compiled only when CONFIG_MESHTASTIC_DEBUG_LOGGING=y.
 * Remove once the overnight-hang root cause is found.
 *
 * Overrides Zephyr's default k_sys_fatal_error_handler (which spins forever
 * with interrupts locked) to: capture the faulting PC/LR/reason into a
 * __noinit RAM region, then reboot via NVIC_SystemReset. On next boot,
 * crash_check_and_report() prints the saved crash context.
 *
 * RAM in the __noinit section is NOT zeroed by Zephyr's C runtime startup,
 * and NVIC_SystemReset does not power-cycle the nRF54L15's SRAM, so the
 * data survives a warm reboot. A magic sentinel guards against interpreting
 * uninitialized RAM as valid crash data.
 */

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_MESHTASTIC_DEBUG_LOGGING)

#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/linker/section_tags.h>

#define CRASH_MAGIC 0xDEADBEEFu

struct crash_info {
    uint32_t magic;
    uint32_t reason;
    uint32_t pc;
    uint32_t lr;
    uint32_t xpsr;
    uint32_t uptime_ms;
    char thread_name[16];
};

static struct crash_info __noinit saved_crash;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    saved_crash.magic = CRASH_MAGIC;
    saved_crash.reason = reason;
    saved_crash.uptime_ms = (uint32_t)k_uptime_get();

    if (esf != NULL) {
        saved_crash.pc = esf->basic.pc;
        saved_crash.lr = esf->basic.lr;
        saved_crash.xpsr = esf->basic.xpsr;
    } else {
        saved_crash.pc = 0;
        saved_crash.lr = 0;
        saved_crash.xpsr = 0;
    }

    const char *name = NULL;
    if (IS_ENABLED(CONFIG_MULTITHREADING) && _current != NULL) {
        name = k_thread_name_get(_current);
    }
    if (name != NULL) {
        size_t i;
        for (i = 0; i < sizeof(saved_crash.thread_name) - 1 && name[i]; i++) {
            saved_crash.thread_name[i] = name[i];
        }
        saved_crash.thread_name[i] = '\0';
    } else {
        saved_crash.thread_name[0] = '?';
        saved_crash.thread_name[1] = '\0';
    }

    LOG_PANIC();
    sys_reboot(SYS_REBOOT_COLD);
    CODE_UNREACHABLE;
}

static const char *reason_str(uint32_t reason)
{
    switch (reason) {
    case K_ERR_CPU_EXCEPTION:  return "CPU exception";
    case K_ERR_SPURIOUS_IRQ:   return "Unhandled IRQ";
    case K_ERR_STACK_CHK_FAIL: return "Stack overflow";
    case K_ERR_KERNEL_OOPS:    return "Kernel oops";
    case K_ERR_KERNEL_PANIC:   return "Kernel panic";
    default:                   return "Unknown";
    }
}

void crash_check_and_report(void)
{
    if (saved_crash.magic != CRASH_MAGIC) {
        return;
    }

    printk("\n!!! PREVIOUS CRASH DETECTED !!!\n");
    printk("  Reason : %s (%u)\n", reason_str(saved_crash.reason), saved_crash.reason);
    printk("  PC     : 0x%08X\n", saved_crash.pc);
    printk("  LR     : 0x%08X\n", saved_crash.lr);
    printk("  xPSR   : 0x%08X\n", saved_crash.xpsr);
    printk("  Uptime : %u ms\n", saved_crash.uptime_ms);
    printk("  Thread : %s\n", saved_crash.thread_name);
    printk("  Use: arm-zephyr-eabi-addr2line -e build/zephyr/zephyr.elf 0x%08X 0x%08X\n",
           saved_crash.pc, saved_crash.lr);
    printk("\n");

    saved_crash.magic = 0;
}

#else /* !CONFIG_MESHTASTIC_DEBUG_LOGGING */

void crash_check_and_report(void) {}

#endif /* CONFIG_MESHTASTIC_DEBUG_LOGGING */
