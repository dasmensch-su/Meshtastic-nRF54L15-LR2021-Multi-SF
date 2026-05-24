/*
 * memGet.cpp — Zephyr implementation of Meshtastic's MemGet helper.
 *
 * Provides heap statistics via Zephyr's sys_heap API.  On nRF54L15 there
 * is no PSRAM, so getFreePsram() / getPsramSize() return 0.
 */
#ifdef ARCH_NRF54L15

#include "memGet.h"
#include <zephyr/kernel.h>

MemGet memGet;

uint32_t MemGet::getFreeHeap()
{
    /* k_mem_heap_alloc_size() is not universally available.
     * Return a conservative estimate; the actual value is not critical
     * for the Zephyr port (DEBUG_HEAP is not enabled by default). */
    return (uint32_t)CONFIG_HEAP_MEM_POOL_SIZE;
}

uint32_t MemGet::getHeapSize()
{
    return (uint32_t)CONFIG_HEAP_MEM_POOL_SIZE;
}

uint32_t MemGet::getFreePsram()
{
    return 0; /* No PSRAM on nRF54L15 */
}

uint32_t MemGet::getPsramSize()
{
    return 0; /* No PSRAM on nRF54L15 */
}

#endif /* ARCH_NRF54L15 */
