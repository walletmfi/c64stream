#ifndef C64U_ATOMIC_H
#define C64U_ATOMIC_H

/**
 * @file c64u-atomic.h
 * @brief Cross-platform atomic operations compatibility
 * 
 * Provides atomic operations that work across POSIX and Windows platforms.
 */

#if defined(_WIN32) && defined(_MSC_VER)
// Windows MSVC: Use Windows-specific atomic intrinsics

#include <Windows.h>
#include <stdbool.h>
#include <stdint.h>

// Windows atomic types
typedef volatile LONG64 atomic_uint64_t;
typedef volatile LONG atomic_uint32_t;
typedef volatile SHORT atomic_uint16_t;
typedef volatile CHAR atomic_bool_t;

// Memory ordering (no-op on Windows)
typedef enum { memory_order_relaxed, memory_order_acquire, memory_order_release, memory_order_acq_rel } memory_order;

// Atomic operations using Windows Interlocked functions
#define atomic_load_explicit(obj, order) (*(obj))
#define atomic_store_explicit(obj, val, order) InterlockedExchange64((obj), (val))
#define atomic_fetch_add_explicit(obj, val, order) InterlockedExchangeAdd64((obj), (val))
#define atomic_exchange_explicit(obj, val, order) InterlockedExchange64((obj), (val))
#define atomic_fetch_or_explicit(obj, val, order) InterlockedOr64((obj), (val))

// Type-specific macros for 32-bit operations
#define atomic_store_explicit_u32(obj, val, order) InterlockedExchange((LONG*)(obj), (val))
#define atomic_fetch_add_explicit_u32(obj, val, order) InterlockedExchangeAdd((LONG*)(obj), (val))
#define atomic_exchange_explicit_u32(obj, val, order) InterlockedExchange((LONG*)(obj), (val))

// Type-specific macros for 16-bit operations
#define atomic_store_explicit_u16(obj, val, order) InterlockedExchange16((SHORT*)(obj), (val))
#define atomic_load_explicit_u16(obj, order) (*(obj))
#define atomic_fetch_add_explicit_u16(obj, val, order) InterlockedExchangeAdd16((SHORT*)(obj), (val))

// Type-specific macros for bool operations
#define atomic_store_explicit_bool(obj, val, order) InterlockedExchange8((CHAR*)(obj), (val) ? 1 : 0)
#define atomic_load_explicit_bool(obj, order) (*(obj) != 0)

// Redefine _Atomic for Windows compatibility
#define _Atomic volatile

#else
// POSIX: Use standard C11 atomics
#include <stdatomic.h>

// Define convenience types
typedef _Atomic uint64_t atomic_uint64_t;
typedef _Atomic uint32_t atomic_uint32_t;
typedef _Atomic uint16_t atomic_uint16_t;
typedef _Atomic bool atomic_bool_t;

// For POSIX, type-specific functions are just aliases to standard functions
#define atomic_store_explicit_u32(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_store_explicit_u16(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_store_explicit_bool(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u32(obj, val, order) atomic_fetch_add_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u16(obj, val, order) atomic_fetch_add_explicit(obj, val, order)
#define atomic_exchange_explicit_u32(obj, val, order) atomic_exchange_explicit(obj, val, order)
#define atomic_load_explicit_u16(obj, order) atomic_load_explicit(obj, order)

#endif

#endif // C64U_ATOMIC_H