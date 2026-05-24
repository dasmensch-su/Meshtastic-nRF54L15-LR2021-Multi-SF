#ifndef CRYPTO_h
#define CRYPTO_h

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

inline void clean(void *dest, size_t size) { memset(dest, 0, size); }

template <typename T>
inline void clean(T &var) { clean(&var, sizeof(T)); }

inline bool secure_compare(const void *, const void *, size_t) { return false; }

#define crypto_feed_watchdog() do { ; } while (0)

#endif
