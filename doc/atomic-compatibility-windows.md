# Windows Atomic Compatibility Implementation

## Problem Statement

The C64U OBS Plugin uses atomic operations for lock-free performance statistics and cross-thread communication. However, Windows builds consistently failed in CI with errors like:

```
C atomic support is not enabled (C11)
```

This occurred despite using MSVC 19.44.35217.0 (Visual Studio 2022 17.12+), which should support C11 atomics.

## Solution: Multi-Tier Atomic Compatibility

### 1. Visual Studio 2022 17.5+ Native C11 Atomics

Visual Studio 2022 17.5+ includes experimental C11 atomics support via:

```cmake
set(_obs_msvc_c_options /MP /Zc:__cplusplus /Zc:preprocessor /std:c17 /experimental:c11atomics)
```

Key features:
- **Native stdatomic.h support** - Same API as POSIX systems
- **ABI compatibility** - Same binary interface as C++ atomics  
- **Lock-free operations** - For objects ≤8 bytes and power-of-two sizes
- **Requires `/std:c11` or `/std:c17`** - Modern C standard mode

### 2. Detection Logic

The compatibility layer detects modern MSVC support:

```c
#if defined(_MSC_VER) && (_MSC_VER >= 1935) && !defined(__STDC_NO_ATOMICS__)
    // Use native C11 atomics
    #include <stdatomic.h>
    typedef _Atomic uint64_t atomic_uint64_t;
    // ... native atomic operations
#else
    // Fallback to Windows Interlocked functions
```

Detection criteria:
- **MSVC 19.35+** (Visual Studio 2022 17.5+)
- **`__STDC_NO_ATOMICS__` not defined** - Indicates atomics are available
- **Automatic fallback** to Interlocked functions for older versions

### 3. Windows Interlocked Fallback

For older MSVC or when C11 atomics fail, uses Windows Interlocked functions:

```c
typedef struct {
    volatile uint64_t value;
} atomic_uint64_t;

#define atomic_store_explicit_u64(obj, val, order) \
    InterlockedExchange64((LONG64 volatile *)&(obj)->value, (LONG64)(val))
```

## Benefits

### 1. **Simplified Maintenance**
- Native C11 atomics eliminate Windows-specific workarounds
- Same API across all platforms (POSIX, Windows, macOS)
- Reduced complexity in atomic header file

### 2. **Better Performance**
- Native atomics use optimal CPU instructions
- Lock-free operations for all supported data types
- ABI compatibility with C++ atomics ensures efficiency

### 3. **Future-Proof**
- Automatically uses native atomics when available
- Graceful fallback for older build environments
- Ready for Visual Studio future releases

### 4. **Build System Integration**
- Single CMake flag enables experimental atomics
- No additional dependencies or setup required
- Works with existing OBS build infrastructure

## Cross-Platform Compatibility

| Platform | Implementation | Status |
|----------|----------------|--------|
| **Linux** | Native C11 atomics (`stdatomic.h`) | ✅ Working |
| **macOS** | Native C11 atomics (`stdatomic.h`) | ✅ Working |
| **Windows Modern** | Experimental C11 atomics (`/experimental:c11atomics`) | 🧪 Testing |
| **Windows Legacy** | Interlocked functions fallback | ✅ Backup |

## Files Modified

1. **`cmake/windows/compilerconfig.cmake`**
   - Added `/experimental:c11atomics` flag to `_obs_msvc_c_options`
   - Enables native stdatomic.h support in MSVC 19.35+

2. **`src/c64u-atomic.h`**
   - Added MSVC version detection logic
   - Uses native C11 atomics when available
   - Maintains Interlocked function fallback
   - Simplified overall complexity

## Testing

The atomic compatibility will be validated by:
- **GitHub Actions CI** - Tests Windows build with MSVC 19.44+
- **Cross-platform builds** - Ensures Linux/macOS remain unaffected
- **Performance benchmarks** - Verifies atomic operations work correctly

## References

- [Visual Studio 2022 17.5 C11 Atomics Blog Post](https://devblogs.microsoft.com/cppblog/c11-atomics-in-visual-studio-2022-version-17-5-preview-2/)
- [MSVC C11/C17 Conformance](https://docs.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance)
- [Windows Interlocked Functions Documentation](https://docs.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access)

This implementation provides robust cross-platform atomic support while leveraging the latest Windows compiler features for optimal performance and maintainability.