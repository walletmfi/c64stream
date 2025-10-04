#ifndef C64U_ATOMIC_H
#define C64U_ATOMIC_H

/**
 * @file c64u-atomic.h
 * @brief Cross-platform atomic operations compatibility
 * 
 * Provides atomic operations that work across POSIX and Windows platforms.
 */

#if defined(_WIN32) && defined(_MSC_VER) && _MSC_VER >= 1944
// Modern MSVC 2022 17.12+ with C17 support - try native atomics first
// If this fails, we fall back to Interlocked functions

// Attempt to use native C11/C17 atomics with proper compiler flags
#if __STDC_VERSION__ >= 201710L || __STDC_VERSION__ >= 201112L
#include <stdatomic.h>

// For modern MSVC, type-specific functions are aliases to standard functions
#define atomic_store_explicit_u32(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_store_explicit_u16(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_store_explicit_bool(obj, val, order) atomic_store_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u32(obj, val, order) atomic_fetch_add_explicit(obj, val, order)
#define atomic_fetch_add_explicit_u16(obj, val, order) atomic_fetch_add_explicit(obj, val, order)
#define atomic_exchange_explicit_u32(obj, val, order) atomic_exchange_explicit(obj, val, order)
#define atomic_load_explicit_u16(obj, order) atomic_load_explicit(obj, order)

#else
// Fallback to Windows Interlocked functions for older MSVC or when C11/C17 atomics fail
#include <Windows.h>
#include <stdbool.h>
#include <stdint.h>

// Memory ordering (simplified for Windows compatibility)
typedef enum { memory_order_relaxed, memory_order_acquire, memory_order_release, memory_order_acq_rel } memory_order;

// Define _Atomic as volatile for Windows compatibility
#define _Atomic volatile

// Atomic operations using Windows Interlocked functions
#define atomic_load_explicit(obj, order) (*(obj))
#define atomic_store_explicit(obj, val, order) ((void)InterlockedExchange64((volatile LONG64*)(obj), (LONG64)(val)))
#define atomic_fetch_add_explicit(obj, val, order) InterlockedExchangeAdd64((volatile LONG64*)(obj), (LONG64)(val))
#define atomic_exchange_explicit(obj, val, order) InterlockedExchange64((volatile LONG64*)(obj), (LONG64)(val))
#define atomic_fetch_or_explicit(obj, val, order) InterlockedOr64((volatile LONG64*)(obj), (LONG64)(val))

// Type-specific operations for Windows
#define atomic_store_explicit_u32(obj, val, order) ((void)InterlockedExchange((volatile LONG*)(obj), (LONG)(val)))
#define atomic_fetch_add_explicit_u32(obj, val, order) InterlockedExchangeAdd((volatile LONG*)(obj), (LONG)(val))
#define atomic_exchange_explicit_u32(obj, val, order) InterlockedExchange((volatile LONG*)(obj), (LONG)(val))
#define atomic_store_explicit_u16(obj, val, order) ((void)InterlockedExchange16((volatile SHORT*)(obj), (SHORT)(val)))
#define atomic_load_explicit_u16(obj, order) (*(obj))
#define atomic_fetch_add_explicit_u16(obj, val, order) InterlockedExchangeAdd16((volatile SHORT*)(obj), (SHORT)(val))
#define atomic_store_explicit_bool(obj, val, order) ((void)InterlockedExchange8((volatile CHAR*)(obj), (val) ? 1 : 0))
#define atomic_load_explicit_bool(obj, order) (*(obj) != 0)
#endif

#elif defined(_WIN32) && defined(_MSC_VER)
// Older MSVC - use Windows Interlocked functions only
#include <Windows.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { memory_order_relaxed, memory_order_acquire, memory_order_release, memory_order_acq_rel } memory_order;
#define _Atomic volatile

#define atomic_load_explicit(obj, order) (*(obj))
#define atomic_store_explicit(obj, val, order) ((void)InterlockedExchange64((volatile LONG64*)(obj), (LONG64)(val)))
#define atomic_fetch_add_explicit(obj, val, order) InterlockedExchangeAdd64((volatile LONG64*)(obj), (LONG64)(val))
#define atomic_exchange_explicit(obj, val, order) InterlockedExchange64((volatile LONG64*)(obj), (LONG64)(val))
#define atomic_fetch_or_explicit(obj, val, order) InterlockedOr64((volatile LONG64*)(obj), (LONG64)(val))

#define atomic_store_explicit_u32(obj, val, order) ((void)InterlockedExchange((volatile LONG*)(obj), (LONG)(val)))
#define atomic_fetch_add_explicit_u32(obj, val, order) InterlockedExchangeAdd((volatile LONG*)(obj), (LONG)(val))
#define atomic_exchange_explicit_u32(obj, val, order) InterlockedExchange((volatile LONG*)(obj), (LONG)(val))
#define atomic_store_explicit_u16(obj, val, order) ((void)InterlockedExchange16((volatile SHORT*)(obj), (SHORT)(val)))
#define atomic_load_explicit_u16(obj, order) (*(obj))
#define atomic_fetch_add_explicit_u16(obj, val, order) InterlockedExchangeAdd16((volatile SHORT*)(obj), (SHORT)(val))
#define atomic_store_explicit_bool(obj, val, order) ((void)InterlockedExchange8((volatile CHAR*)(obj), (val) ? 1 : 0))
#define atomic_load_explicit_bool(obj, order) (*(obj) != 0)

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