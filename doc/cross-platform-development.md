# Cross-Platform C Development Guide for C64U-OBS

This document outlines the key differences and best practices for writing portable C code that works across Ubuntu (Linux), macOS, and Windows platforms in the C64U-OBS project.

## Platform-Specific Include Files

### Unix/POSIX Headers (Linux/macOS)
```c
#include <unistd.h>        // POSIX functions (sleep, usleep, fork, etc.)
#include <sys/socket.h>    // Socket functions
#include <netinet/in.h>    // Internet address structures
#include <arpa/inet.h>     // Internet address conversion functions
#include <sys/wait.h>      // Process waiting functions
#include <pthread.h>       // POSIX threads
#include <fcntl.h>         // File control options
#include <errno.h>         // Error number definitions
```

### Windows Headers
```c
#include <winsock2.h>      // Windows socket API
#include <ws2tcpip.h>      // Windows socket extensions
#include <windows.h>       // Windows API
#include <io.h>            // Windows I/O functions
#include <process.h>       // Windows process functions
```

## Platform Detection Macros

```c
#ifdef _WIN32
    // Windows-specific code
#elif defined(__APPLE__)
    // macOS-specific code
#elif defined(__linux__)
    // Linux-specific code
#else
    // Generic POSIX code
#endif
```

## Socket Programming Differences

### Socket Types and Functions
```c
#ifdef _WIN32
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define close(s) closesocket(s)
    #define SHUT_RDWR SD_BOTH
    typedef int socklen_t;
#else
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
    // Use standard close() and SHUT_RDWR
#endif
```

### Socket Initialization
```c
#ifdef _WIN32
    // Windows requires WSAStartup
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
```

## Threading Differences

### POSIX Threads vs Windows Threads
```c
#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE thread_handle_t;
    typedef DWORD (WINAPI *thread_func_t)(LPVOID);
#else
    #include <pthread.h>
    typedef pthread_t thread_handle_t;
    typedef void* (*thread_func_t)(void*);
#endif
```

## Process Management

### Fork vs CreateProcess
```c
#ifdef _WIN32
    // Windows uses CreateProcess instead of fork
    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    CreateProcess(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
#else
    // Unix/Linux uses fork/exec
    pid_t pid = fork();
    if (pid == 0) {
        execl(program, program, args, NULL);
    }
#endif
```

## Data Type Compatibility

### Size-Specific Types
```c
#ifdef _WIN32
    // Windows MSVC doesn't have ssize_t
    #ifndef __MINGW32__
        typedef long long ssize_t;
    #endif
    #define SSIZE_T_FORMAT "%lld"
    #define SSIZE_T_CAST(x) ((long long)(x))
#else
    #define SSIZE_T_FORMAT "%zd"
    #define SSIZE_T_CAST(x) (x)
#endif
```

## Compiler Flags and Warnings

### GCC/Clang vs MSVC
```cmake
if(MSVC)
    # MSVC flags
    target_compile_options(target PRIVATE /W4)
    # Disable specific warnings if needed
    target_compile_options(target PRIVATE /wd4996)  # Disable deprecation warnings
else()
    # GCC/Clang flags
    target_compile_options(target PRIVATE -Wall -Wextra -std=c17)
endif()
```

### Common Warning Suppressions
- **Unused variables**: Use `(void)variable;` or `__attribute__((unused))` (GCC) / `#pragma warning(suppress: 4101)` (MSVC)
- **Deprecated functions**: Use modern alternatives or suppress warnings selectively

## Sleep Functions

### Platform-Specific Sleep
```c
#ifdef _WIN32
    #include <windows.h>
    #define sleep_ms(ms) Sleep(ms)
    #define sleep_us(us) Sleep((us) / 1000)
#else
    #include <unistd.h>
    #define sleep_ms(ms) usleep((ms) * 1000)
    #define sleep_us(us) usleep(us)
#endif
```

## File System Differences

### Path Separators
```c
#ifdef _WIN32
    #define PATH_SEPARATOR "\\"
#else
    #define PATH_SEPARATOR "/"
#endif
```

## CMake Configuration Best Practices

### Platform-Specific Linking
```cmake
if(WIN32)
    target_link_libraries(target ws2_32)
elseif(UNIX)
    target_link_libraries(target pthread m)
    if(NOT APPLE)
        # Linux-specific libraries
        target_link_libraries(target rt)
    endif()
endif()
```

### Test Configuration with Xcode
```cmake
# For Xcode generator, tests need configuration
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_XCODE_GENERATE_SCHEME ON)
endif()
```

## Header Guards and Feature Detection

### Feature Test Macros
```c
#ifndef _WIN32
    #define _GNU_SOURCE        // Enable GNU extensions on Linux
    #define _POSIX_C_SOURCE 200809L  // Enable POSIX.1-2008 features
#endif
```

## Error Handling Differences

### Socket Error Handling
```c
#ifdef _WIN32
    int get_socket_error() { return WSAGetLastError(); }
    const char* get_socket_error_string(int error) {
        // Use FormatMessage or custom lookup
    }
#else
    int get_socket_error() { return errno; }
    const char* get_socket_error_string(int error) {
        return strerror(error);
    }
#endif
```

## Best Practices Summary

1. **Always use platform detection macros** for conditional compilation
2. **Abstract platform differences** behind common interfaces
3. **Use CMake for build system abstraction** rather than hardcoded flags
4. **Test on all target platforms** regularly, not just at the end
5. **Use standard C library functions** when possible, falling back to platform-specific only when necessary
6. **Be explicit about data types** and their sizes across platforms
7. **Handle compiler warnings consistently** across all platforms
8. **Use feature test macros** to enable required functionality
9. **Isolate platform-specific code** in separate functions or files when complexity grows
10. **Document platform-specific requirements** clearly

## Testing Strategy

- **Local testing**: Test on available platform first (usually Linux)
- **Cross-compilation testing**: Use CMake presets for different platforms
- **CI/CD validation**: Ensure all platforms build and test successfully
- **Incremental fixes**: Address one platform at a time, then verify others still work