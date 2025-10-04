#ifndef C64U_ATOMIC_H
#define C64U_ATOMIC_H

/**
 * @file c64u-atomic.h
 * @brief Cross-platform atomic operations compatibility
 * 
 * Provides consistent atomic operations across platforms, with fallbacks
 * for MSVC which has limited C11 atomic support.
 */

#ifdef _WIN32
// Windows: Use compiler intrinsics as fallback for limited C11 support
#include <Windows.h>
#include <intrin.h>

// Define atomic types using volatile for Windows
#define _Atomic volatile

// Memory ordering - simplified for Windows compatibility
typedef enum {
    memory_order_relaxed = 0,
    memory_order_acquire = 1,
    memory_order_release = 2,
    memory_order_acq_rel = 3
} memory_order;

// Atomic load operations
static inline uint64_t atomic_load_explicit(volatile uint64_t *obj, memory_order order)
{
    (void)order; // Unused on Windows
#ifdef _WIN64
    return InterlockedOr64((volatile LONG64 *)obj, 0);
#else
    // For 32-bit Windows, use critical section or simpler approach
    return *obj;
#endif
}

static inline uint32_t atomic_load_explicit_u32(volatile uint32_t *obj, memory_order order)
{
    (void)order;
    return InterlockedOr((volatile LONG *)obj, 0);
}

static inline uint16_t atomic_load_explicit_u16(volatile uint16_t *obj, memory_order order)
{
    (void)order;
    return InterlockedOr16((volatile SHORT *)obj, 0);
}

static inline bool atomic_load_explicit_bool(volatile bool *obj, memory_order order)
{
    (void)order;
    return !!InterlockedOr8((volatile char *)obj, 0);
}

// Atomic store operations
static inline void atomic_store_explicit(volatile uint64_t *obj, uint64_t val, memory_order order)
{
    (void)order;
#ifdef _WIN64
    InterlockedExchange64((volatile LONG64 *)obj, val);
#else
    *obj = val;
#endif
}

static inline void atomic_store_explicit_u32(volatile uint32_t *obj, uint32_t val, memory_order order)
{
    (void)order;
    InterlockedExchange((volatile LONG *)obj, val);
}

static inline void atomic_store_explicit_u16(volatile uint16_t *obj, uint16_t val, memory_order order)
{
    (void)order;
    InterlockedExchange16((volatile SHORT *)obj, val);
}

static inline void atomic_store_explicit_bool(volatile bool *obj, bool val, memory_order order)
{
    (void)order;
    InterlockedExchange8((volatile char *)obj, val ? 1 : 0);
}

// Atomic fetch-add operations
static inline uint64_t atomic_fetch_add_explicit(volatile uint64_t *obj, uint64_t val, memory_order order)
{
    (void)order;
#ifdef _WIN64
    return InterlockedExchangeAdd64((volatile LONG64 *)obj, val);
#else
    uint64_t old = *obj;
    *obj += val;
    return old;
#endif
}

static inline uint32_t atomic_fetch_add_explicit_u32(volatile uint32_t *obj, uint32_t val, memory_order order)
{
    (void)order;
    return InterlockedExchangeAdd((volatile LONG *)obj, val);
}

static inline uint16_t atomic_fetch_add_explicit_u16(volatile uint16_t *obj, uint16_t val, memory_order order)
{
    (void)order;
    return InterlockedExchangeAdd16((volatile SHORT *)obj, val);
}

// Atomic exchange operations
static inline uint64_t atomic_exchange_explicit(volatile uint64_t *obj, uint64_t val, memory_order order)
{
    (void)order;
#ifdef _WIN64
    return InterlockedExchange64((volatile LONG64 *)obj, val);
#else
    uint64_t old = *obj;
    *obj = val;
    return old;
#endif
}

static inline uint32_t atomic_exchange_explicit_u32(volatile uint32_t *obj, uint32_t val, memory_order order)
{
    (void)order;
    return InterlockedExchange((volatile LONG *)obj, val);
}

// Atomic fetch-or operations
static inline uint64_t atomic_fetch_or_explicit(volatile uint64_t *obj, uint64_t val, memory_order order)
{
    (void)order;
#ifdef _WIN64
    return InterlockedOr64((volatile LONG64 *)obj, val);
#else
    uint64_t old = *obj;
    *obj |= val;
    return old;
#endif
}

// Overloaded macros for type-generic operations
#define atomic_load_explicit(obj, order) _Generic((obj), \
    volatile uint64_t*: atomic_load_explicit, \
    volatile uint32_t*: atomic_load_explicit_u32, \
    volatile uint16_t*: atomic_load_explicit_u16, \
    volatile bool*: atomic_load_explicit_bool \
    )(obj, order)

#define atomic_store_explicit(obj, val, order) _Generic((obj), \
    volatile uint64_t*: atomic_store_explicit, \
    volatile uint32_t*: atomic_store_explicit_u32, \
    volatile uint16_t*: atomic_store_explicit_u16, \
    volatile bool*: atomic_store_explicit_bool \
    )(obj, val, order)

#define atomic_fetch_add_explicit(obj, val, order) _Generic((obj), \
    volatile uint64_t*: atomic_fetch_add_explicit, \
    volatile uint32_t*: atomic_fetch_add_explicit_u32, \
    volatile uint16_t*: atomic_fetch_add_explicit_u16 \
    )(obj, val, order)

#define atomic_exchange_explicit(obj, val, order) _Generic((obj), \
    volatile uint64_t*: atomic_exchange_explicit, \
    volatile uint32_t*: atomic_exchange_explicit_u32 \
    )(obj, val, order)

#else
// POSIX: Use standard C11 atomics
#include <stdatomic.h>
#endif

#endif // C64U_ATOMIC_H