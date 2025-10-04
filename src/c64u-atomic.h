#ifndef C64U_ATOMIC_H
#define C64U_ATOMIC_H

/**
 * @file c64u-atomic.h
 * @brief Cross-platform atomic opera#def#def// Generic // atomic_fetch_or_explicit for bit mask operations
#define atomic_fetch_or_explicit_u64(obj, val, order) InterlockedOr64((LONG64 volatile *)&(obj)->value, (LONG64)(val))
#define atomic_fetch_or_explicit(obj, val, order) atomic_fetch_or_explicit_u64(obj, val, order)c functions - use type-specific versions in the actual code
// These are provided for compatibility but actual code should use type-specific functions
#define atomic_load_explicit(obj, order) atomic_load_explicit_u64(obj, order)
#define atomic_store_explicit(obj, val, order) atomic_store_explicit_u64(obj, val, order)
#define atomic_fetch_add_explicit(obj, val, order) atomic_fetch_add_explicit_u64(obj, val, order)
#define atomic_exchange_explicit(obj, val, order) atomic_exchange_explicit_u64(obj, val, order)tomic_exchange_explicit(obj, val, order) _Generic((obj), \\
    atomic_uint64_t *: atomic_exchange_explicit_u64, \\
    atomic_uint32_t *: atomic_exchange_explicit_u32, \\
    atomic_uint16_t *: atomic_store_explicit_u16 \\
)(obj, val, order)tomic_exchange_explicit(obj, val, order) _Generic((obj), \\
    atomic_uint64_t *: atomic_exchange_explicit_u64, \\
    atomic_uint32_t *: atomic_exchange_explicit_u32, \\
    atomic_uint16_t *: atomic_store_explicit_u16 \\
)(obj, val, order) compatibility layer
 *
 * This header provides a unified interface for atomic operations across different platforms:
 * - Windows: Uses Windows Interlocked functions for reliable cross-version compatibility
 * - POSIX: Uses standard C11 atomics (stdatomic.h)
 *
 * All atomic operations use relaxed memory ordering for maximum performance.
 *
 * IMPORTANT: On Windows, this header uses minimal Windows headers to avoid conflicts.
 * If your .c file includes both this header and c64u-network.h, include c64u-network.h
 * FIRST to prevent winsock.h vs winsock2.h conflicts.
 */

#ifdef _WIN32

// Windows: Use Interlocked functions for reliable cross-version compatibility
// This provides consistent atomic operations across all MSVC versions without
// depending on experimental C11 atomics support which varies by compiler version

// Define target architecture to prevent "No Target Architecture" error
#if defined(_M_X64) || defined(__x86_64__)
#define _AMD64_
#elif defined(_M_IX86) || defined(__i386__)
#define _X86_
#elif defined(_M_ARM64) || defined(__aarch64__)
#define _ARM64_
#elif defined(_M_ARM) || defined(__arm__)
#define _ARM_
#endif

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#define NOMINMAX            // Prevent Windows.h from defining min and max as macros
#include <windef.h>         // Basic Windows types
#include <winbase.h>        // For Interlocked functions
#include <intrin.h>
#include <stdint.h>
#include <stdbool.h>

// Windows: Use Interlocked functions for atomic operations
typedef struct {
    volatile uint64_t value;
} atomic_uint64_t;

typedef struct {
    volatile uint32_t value;
} atomic_uint32_t;

typedef struct {
    volatile uint32_t value; // Use 32-bit for compatibility with Interlocked functions
} atomic_uint16_t;

typedef struct {
    volatile uint32_t value; // Use 32-bit for compatibility with Interlocked functions
} atomic_bool_t;

// Memory ordering constants (unused on Windows, but defined for consistency)
typedef enum {
    memory_order_relaxed = 0,
    memory_order_consume = 1,
    memory_order_acquire = 2,
    memory_order_release = 3,
    memory_order_acq_rel = 4,
    memory_order_seq_cst = 5
} memory_order;

// Windows atomic functions using Interlocked operations

// Inline functions for _Generic dispatch (needed because _Generic can't use macros)
static inline uint64_t atomic_load_explicit_u64_func(atomic_uint64_t *obj, memory_order order)
{
    return (uint64_t)(obj->value);
}
static inline uint64_t atomic_store_explicit_u64_func(atomic_uint64_t *obj, uint64_t val, memory_order order)
{
    return InterlockedExchange64((LONG64 volatile *)&obj->value, (LONG64)val);
}
static inline uint64_t atomic_fetch_add_explicit_u64_func(atomic_uint64_t *obj, uint64_t val, memory_order order)
{
    return InterlockedExchangeAdd64((LONG64 volatile *)&obj->value, (LONG64)val);
}
static inline uint64_t atomic_exchange_explicit_u64_func(atomic_uint64_t *obj, uint64_t val, memory_order order)
{
    return InterlockedExchange64((LONG64 volatile *)&obj->value, (LONG64)val);
}
static inline uint64_t atomic_fetch_or_explicit_u64_func(atomic_uint64_t *obj, uint64_t val, memory_order order)
{
    return InterlockedOr64((LONG64 volatile *)&obj->value, (LONG64)val);
}

static inline uint32_t atomic_load_explicit_u32_func(atomic_uint32_t *obj, memory_order order)
{
    return (uint32_t)(obj->value);
}
static inline uint32_t atomic_store_explicit_u32_func(atomic_uint32_t *obj, uint32_t val, memory_order order)
{
    return InterlockedExchange((LONG volatile *)&obj->value, (LONG)val);
}
static inline uint32_t atomic_fetch_add_explicit_u32_func(atomic_uint32_t *obj, uint32_t val, memory_order order)
{
    return InterlockedAdd((LONG volatile *)&obj->value, (LONG)val) - (LONG)val;
}
static inline uint32_t atomic_exchange_explicit_u32_func(atomic_uint32_t *obj, uint32_t val, memory_order order)
{
    return InterlockedExchange((LONG volatile *)&obj->value, (LONG)val);
}
static inline uint32_t atomic_fetch_or_explicit_u32_func(atomic_uint32_t *obj, uint32_t val, memory_order order)
{
    return InterlockedOr((LONG volatile *)&obj->value, (LONG)val);
}

static inline uint16_t atomic_load_explicit_u16_func(atomic_uint16_t *obj, memory_order order)
{
    return (uint16_t)(obj->value);
}
static inline uint16_t atomic_store_explicit_u16_func(atomic_uint16_t *obj, uint16_t val, memory_order order)
{
    return InterlockedExchange((LONG volatile *)&obj->value, (LONG)val);
}
static inline uint16_t atomic_fetch_add_explicit_u16_func(atomic_uint16_t *obj, uint16_t val, memory_order order)
{
    return InterlockedExchangeAdd((LONG volatile *)&obj->value, (LONG)val);
}

static inline bool atomic_load_explicit_bool_func(atomic_bool_t *obj, memory_order order)
{
    return (bool)(obj->value);
}
static inline bool atomic_store_explicit_bool_func(atomic_bool_t *obj, bool val, memory_order order)
{
    return InterlockedExchange((LONG volatile *)&obj->value, (LONG)val);
}

// Macro versions (for direct use)
#define atomic_load_explicit_u64(obj, order) ((uint64_t)((obj)->value))
#define atomic_store_explicit_u64(obj, val, order) InterlockedExchange64((LONG64 volatile *)&(obj)->value, (LONG64)(val))
#define atomic_fetch_add_explicit_u64(obj, val, order) InterlockedExchangeAdd64((LONG64 volatile *)&(obj)->value, (LONG64)(val))
#define atomic_exchange_explicit_u64(obj, val, order) InterlockedExchange64((LONG64 volatile *)&(obj)->value, (LONG64)(val))

#define atomic_load_explicit_u32(obj, order) ((uint32_t)((obj)->value))
#define atomic_store_explicit_u32(obj, val, order) InterlockedExchange((LONG volatile *)&(obj)->value, (LONG)(val))
#define atomic_fetch_add_explicit_u32(obj, val, order) InterlockedAdd((LONG volatile *)&(obj)->value, (LONG)(val)) - (LONG)(val)
#define atomic_exchange_explicit_u32(obj, val, order) InterlockedExchange((LONG volatile *)&(obj)->value, (LONG)(val))

#define atomic_load_explicit_u16(obj, order) ((uint16_t)((obj)->value))
#define atomic_store_explicit_u16(obj, val, order) InterlockedExchange((LONG volatile *)&(obj)->value, (LONG)(val))
#define atomic_fetch_add_explicit_u16(obj, val, order) InterlockedExchangeAdd((LONG volatile *)&(obj)->value, (LONG)(val))

#define atomic_load_explicit_bool(obj, order) ((bool)((obj)->value))
#define atomic_store_explicit_bool(obj, val, order) InterlockedExchange((LONG volatile *)&(obj)->value, (LONG)(val))

// Generic atomic functions using _Generic for type dispatch
#define atomic_load_explicit(obj, order) \
    _Generic((obj), \
        atomic_uint64_t*: atomic_load_explicit_u64_func, \
        atomic_uint32_t*: atomic_load_explicit_u32_func, \
        atomic_uint16_t*: atomic_load_explicit_u16_func, \
        atomic_bool_t*: atomic_load_explicit_bool_func, \
        default: atomic_load_explicit_u64_func \
    )(obj, order)

#define atomic_store_explicit(obj, val, order) \
    _Generic((obj), \
        atomic_uint64_t*: atomic_store_explicit_u64_func, \
        atomic_uint32_t*: atomic_store_explicit_u32_func, \
        atomic_uint16_t*: atomic_store_explicit_u16_func, \
        atomic_bool_t*: atomic_store_explicit_bool_func, \
        default: atomic_store_explicit_u64_func \
    )(obj, val, order)

#define atomic_fetch_add_explicit(obj, val, order) \
    _Generic((obj), \
        atomic_uint64_t*: atomic_fetch_add_explicit_u64_func, \
        atomic_uint32_t*: atomic_fetch_add_explicit_u32_func, \
        atomic_uint16_t*: atomic_fetch_add_explicit_u16_func, \
        default: atomic_fetch_add_explicit_u64_func \
    )(obj, val, order)

#define atomic_exchange_explicit(obj, val, order) \
    _Generic((obj), \
        atomic_uint64_t*: atomic_exchange_explicit_u64_func, \
        atomic_uint32_t*: atomic_exchange_explicit_u32_func, \
        default: atomic_exchange_explicit_u64_func \
    )(obj, val, order)

#define atomic_fetch_or_explicit(obj, val, order) \
    _Generic((obj), \
        atomic_uint64_t*: atomic_fetch_or_explicit_u64_func, \
        atomic_uint32_t*: atomic_fetch_or_explicit_u32_func, \
        default: atomic_fetch_or_explicit_u64_func \
    )(obj, val, order)

// Convenience macros for specific types to avoid ambiguity
#define atomic_load_u64(obj) atomic_load_explicit_u64(obj, memory_order_seq_cst)
#define atomic_store_u64(obj, val) atomic_store_explicit_u64(obj, val, memory_order_seq_cst)
#define atomic_fetch_add_u64(obj, val) atomic_fetch_add_explicit_u64(obj, val, memory_order_seq_cst)

#define atomic_load_u32(obj) atomic_load_explicit_u32(obj, memory_order_seq_cst)
#define atomic_store_u32(obj, val) atomic_store_explicit_u32(obj, val, memory_order_seq_cst)
#define atomic_fetch_add_u32(obj, val) atomic_fetch_add_explicit_u32(obj, val, memory_order_seq_cst)

#define atomic_load_u16(obj) atomic_load_explicit_u16(obj, memory_order_seq_cst)
#define atomic_store_u16(obj, val) atomic_store_explicit_u16(obj, val, memory_order_seq_cst)
#define atomic_fetch_add_u16(obj, val) atomic_fetch_add_explicit_u16(obj, val, memory_order_seq_cst)

// atomic_fetch_or_explicit for bit mask operations (macro versions)
#define atomic_fetch_or_explicit_u64(obj, val, order) InterlockedOr64((LONG64 volatile *)&(obj)->value, (LONG64)(val))
#define atomic_fetch_or_explicit_u32(obj, val, order) InterlockedOr((LONG volatile *)&(obj)->value, (LONG)(val))

// Initialize atomic variables
#define ATOMIC_VAR_INIT(val) { (val) }

#else
// POSIX: Use standard C11 atomics
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

// Define convenience types
typedef _Atomic uint64_t atomic_uint64_t;
typedef _Atomic uint32_t atomic_uint32_t;
typedef _Atomic uint16_t atomic_uint16_t;
typedef _Atomic bool atomic_bool_t;

// For POSIX, use native C11 atomic functions
#define atomic_load_explicit_u64(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_u64(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u64(obj, val, order) atomic_fetch_add_explicit(obj, val, order)

#define atomic_load_explicit_u32(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_u32(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u32(obj, val, order) atomic_fetch_add_explicit(obj, val, order)
#define atomic_exchange_explicit_u32(obj, val, order) atomic_exchange_explicit(obj, val, order)
#define atomic_fetch_or_explicit_u32(obj, val, order) atomic_fetch_or_explicit(obj, val, order)

#define atomic_load_explicit_u16(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_u16(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u16(obj, val, order) atomic_fetch_add_explicit(obj, val, order)

#define atomic_load_explicit_bool(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_bool(obj, val, order) atomic_store_explicit(obj, val, order)

// Add missing fetch_or operations
#define atomic_fetch_or_explicit_u64(obj, val, order) atomic_fetch_or_explicit(obj, val, order)

#endif

#ifdef _WIN32
// Windows: Convenience macros for relaxed memory ordering
#define atomic_load(obj) atomic_load_explicit_u32(obj, memory_order_relaxed)
#define atomic_store(obj, val) atomic_store_explicit_u32(obj, val, memory_order_relaxed)
#define atomic_fetch_add(obj, val) atomic_fetch_add_explicit_u32(obj, val, memory_order_relaxed)
// Additional convenience macros for atomic exchanges
#define atomic_exchange(obj, val) atomic_exchange_explicit_u32(obj, val, memory_order_relaxed)

#else
// POSIX: Use standard C11 atomic functions directly - they already exist
// Define the typed versions using standard functions
#define atomic_load_u64(obj) atomic_load_explicit(obj, memory_order_relaxed)
#define atomic_store_u64(obj, val) atomic_store_explicit(obj, val, memory_order_relaxed)
#define atomic_fetch_add_u64(obj, val) atomic_fetch_add_explicit(obj, val, memory_order_relaxed)

#define atomic_load_u16(obj) atomic_load_explicit(obj, memory_order_relaxed)
#define atomic_store_u16(obj, val) atomic_store_explicit(obj, val, memory_order_relaxed)
#define atomic_fetch_add_u16(obj, val) atomic_fetch_add_explicit(obj, val, memory_order_relaxed)

#define atomic_load_bool(obj) atomic_load_explicit(obj, memory_order_relaxed)
#define atomic_store_bool(obj, val) atomic_store_explicit(obj, val, memory_order_relaxed)

#endif

#endif // C64U_ATOMIC_H
