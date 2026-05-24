#pragma once
#ifdef ARCH_NRF54L15

#include <zephyr/kernel.h>
#include <stdint.h>

/* BaseType_t is defined as uint32_t in freertosinc.h for non-FreeRTOS builds */
#include "../freertosinc.h"

namespace concurrency
{

/**
 * Binary semaphore backed by Zephyr k_sem.
 *
 * Replaces BinarySemaphorePosix (which was a stub that just called delay())
 * with a real semaphore that supports give-from-ISR and timed take.
 */
class BinarySemaphoreZephyr
{
  public:
    BinarySemaphoreZephyr();
    ~BinarySemaphoreZephyr();

    /** Returns false if we timed out (i.e. were not interrupted) */
    bool take(uint32_t msec);

    void give();

    void giveFromISR(BaseType_t *pxHigherPriorityTaskWoken);

  private:
    struct k_sem sem_;
};

} // namespace concurrency

#endif /* ARCH_NRF54L15 */
