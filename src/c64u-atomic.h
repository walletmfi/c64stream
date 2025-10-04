#ifndef C64U_ATOMIC_H
#define C64U_ATOMIC_H

/**
 * @file c64u-atomic.h
 * @brief Cross-platform atomic operations compatibility layer
 *
 * This header provides a unified interface for atomic operations across different platforms:
 * - Modern MSVC 19.35+ (VS2022 17.5+): Uses native C11 atomics with /experimental:c11atomics
 * - Older Windows: Uses Windows Interlocked functions for backward compatibility
 * - POSIX: Uses standard C11 atomics (stdatomic.h)
 *
 * All atomic operations use relaxed memory ordering for maximum performance.
 */

#ifdef _WIN32

// Check for modern MSVC with experimental C11 atomics support
// Visual Studio 2022 17.5+ (MSVC 19.35+) supports /experimental:c11atomics
// We also check for the absence of __STDC_NO_ATOMICS__ macro which indicates atomics are available
#if defined(_MSC_VER) && (_MSC_VER >= 1935) && !defined(__STDC_NO_ATOMICS__)

// Modern MSVC: Use native C11 atomics with experimental support
#include <stdatomic.h>

// Define convenience types using native C11 atomics
typedef _Atomic uint64_t atomic_uint64_t;
typedef _Atomic uint32_t atomic_uint32_t;
typedef _Atomic uint16_t atomic_uint16_t;
typedef _Atomic bool atomic_bool_t;

// Use native C11 atomic functions directly
#define atomic_load_explicit_u64(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_u64(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u64(obj, val, order) atomic_fetch_add_explicit(obj, val, order)

#define atomic_load_explicit_u32(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_u32(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u32(obj, val, order) atomic_fetch_add_explicit(obj, val, order)
#define atomic_exchange_explicit_u32(obj, val, order) atomic_exchange_explicit(obj, val, order)

#define atomic_load_explicit_u16(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_u16(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u16(obj, val, order) atomic_fetch_add_explicit(obj, val, order)

#define atomic_load_explicit_bool(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_bool(obj, val, order) atomic_store_explicit(obj, val, order)

#else

// Fallback for older Windows: Use Interlocked functions
#include <windows.h>
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
    volatile uint16_t value;
} atomic_uint16_t;

typedef struct {
    volatile bool value;
} atomic_bool_t;

// Memory ordering constants (unused on Windows, but defined for consistency)
typedef enum { memory_order_relaxed = 0 } memory_order;

// Windows atomic functions using Interlocked operations
#define atomic_load_explicit_u64(obj, order) ((uint64_t)((obj)->value))
#define atomic_store_explicit_u64(obj, val, order) InterlockedExchange64((LONG64 volatile *)&(obj)->value, (LONG64)(val))
#define atomic_fetch_add_explicit_u64(obj, val, order) InterlockedAdd64((LONG64 volatile *)&(obj)->value, (LONG64)(val)) - (LONG64)(val)

#define atomic_load_explicit_u32(obj, order) ((uint32_t)((obj)->value))
#define atomic_store_explicit_u32(obj, val, order) InterlockedExchange((LONG volatile *)&(obj)->value, (LONG)(val))
#define atomic_fetch_add_explicit_u32(obj, val, order) InterlockedAdd((LONG volatile *)&(obj)->value, (LONG)(val)) - (LONG)(val)
#define atomic_exchange_explicit_u32(obj, val, order) InterlockedExchange((LONG volatile *)&(obj)->value, (LONG)(val))

#define atomic_load_explicit_u16(obj, order) ((uint16_t)((obj)->value))
#define atomic_store_explicit_u16(obj, val, order) InterlockedExchange16((SHORT volatile *)&(obj)->value, (SHORT)(val))
#define atomic_fetch_add_explicit_u16(obj, val, order) InterlockedAdd16((SHORT volatile *)&(obj)->value, (SHORT)(val)) - (SHORT)(val)

#define atomic_load_explicit_bool(obj, order) ((bool)((obj)->value))
#define atomic_store_explicit_bool(obj, val, order) InterlockedExchange8((char volatile *)&(obj)->value, (char)(val))

// Initialize atomic variables
#define ATOMIC_VAR_INIT(val) { (val) }

#endif

#else
// POSIX: Use standard C11 atomics
#include <stdatomic.h>

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

#define atomic_load_explicit_u16(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_u16(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u16(obj, val, order) atomic_fetch_add_explicit(obj, val, order)

#define atomic_load_explicit_bool(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_bool(obj, val, order) atomic_store_explicit(obj, val, order)

#endif

#endif // C64U_ATOMIC_H
