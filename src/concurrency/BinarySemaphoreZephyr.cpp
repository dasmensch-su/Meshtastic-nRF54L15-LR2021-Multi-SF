#ifdef ARCH_NRF54L15
#include "BinarySemaphoreZephyr.h"

namespace concurrency
{

BinarySemaphoreZephyr::BinarySemaphoreZephyr()
{
    /* Binary semaphore: initial count 0, max count 1 */
    k_sem_init(&sem_, 0, 1);
}

BinarySemaphoreZephyr::~BinarySemaphoreZephyr() {}

/**
 * Block until the semaphore is given or msec elapses.
 * Returns true if the semaphore was taken (i.e. we were interrupted),
 * false on timeout — matching the InterruptableDelay convention.
 */
bool BinarySemaphoreZephyr::take(uint32_t msec)
{
    int ret = k_sem_take(&sem_, K_MSEC(msec));
    return (ret == 0); /* 0 = taken (interrupted), -EAGAIN = timed out */
}

void BinarySemaphoreZephyr::give()
{
    k_sem_give(&sem_);
}

/*
 * Give the semaphore from ISR context.
 * k_sem_give() is ISR-safe in Zephyr; pxHigherPriorityTaskWoken is a
 * FreeRTOS-ism that has no meaning here — we ignore it.
 */
void BinarySemaphoreZephyr::giveFromISR(BaseType_t *pxHigherPriorityTaskWoken)
{
    (void)pxHigherPriorityTaskWoken;
    k_sem_give(&sem_);
}

} // namespace concurrency

#endif /* ARCH_NRF54L15 */
