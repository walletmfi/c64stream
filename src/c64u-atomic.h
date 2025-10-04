#ifndef C64U_ATOMIC_H
#define C64U_ATOMIC_H

/**
 * @file c64u-atomic.h
 * @brief Cross-platform atomic operations compatibility layer
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
#define WIN32_LEAN   // Exclude rarely-used stuff from Windows headers
#define NOMINMAX     // Prevent Windows.h from defining min and max as macros
#include <windef.h>  // Basic Windows types
#include <winbase.h> // For Interlocked functions
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
#define atomic_store_explicit_u16(obj, val, order) InterlockedExchange((LONG volatile *)&(obj)->value, (LONG)(val))
#define atomic_fetch_add_explicit_u16(obj, val, order) InterlockedExchangeAdd((LONG volatile *)&(obj)->value, (LONG)(val))

#define atomic_load_explicit_bool(obj, order) ((bool)((obj)->value))
#define atomic_store_explicit_bool(obj, val, order) InterlockedExchange((LONG volatile *)&(obj)->value, (LONG)(val))

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

#define atomic_load_explicit_u16(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_u16(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u16(obj, val, order) atomic_fetch_add_explicit(obj, val, order)

#define atomic_load_explicit_bool(obj, order) atomic_load_explicit(obj, order)
#define atomic_store_explicit_bool(obj, val, order) atomic_store_explicit(obj, val, order)

#endif

#ifdef _WIN32
// Windows: Convenience macros for relaxed memory ordering
#define atomic_load(obj) atomic_load_explicit_u32(obj, memory_order_relaxed)
#define atomic_store(obj, val) atomic_store_explicit_u32(obj, val, memory_order_relaxed)
#define atomic_fetch_add(obj, val) atomic_fetch_add_explicit_u32(obj, val, memory_order_relaxed)
#define atomic_exchange(obj, val) atomic_exchange_explicit_u32(obj, val, memory_order_relaxed)

#define atomic_load_u64(obj) atomic_load_explicit_u64(obj, memory_order_relaxed)
#define atomic_store_u64(obj, val) atomic_store_explicit_u64(obj, val, memory_order_relaxed)
#define atomic_fetch_add_u64(obj, val) atomic_fetch_add_explicit_u64(obj, val, memory_order_relaxed)

#define atomic_load_u16(obj) atomic_load_explicit_u16(obj, memory_order_relaxed)
#define atomic_store_u16(obj, val) atomic_store_explicit_u16(obj, val, memory_order_relaxed)
#define atomic_fetch_add_u16(obj, val) atomic_fetch_add_explicit_u16(obj, val, memory_order_relaxed)

#define atomic_load_bool(obj) atomic_load_explicit_bool(obj, memory_order_relaxed)
#define atomic_store_bool(obj, val) atomic_store_explicit_bool(obj, val, memory_order_relaxed)

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
