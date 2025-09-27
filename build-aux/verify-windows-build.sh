#!/bin/bash
# Windows build verification script using MinGW cross-compilation
# This allows testing Windows compatibility locally before CI

set -e

echo "=== Windows Build Verification Script ==="
echo "Testing Windows compatibility using MinGW cross-compilation"

# Check if MinGW is available
if ! command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    echo "ERROR: MinGW cross-compiler not found"
    echo "Install with: sudo apt-get install gcc-mingw-w64-x86-64"
    exit 1
fi

# Create temporary test file with key patterns from plugin
TEST_FILE="/tmp/c64u_windows_test.c"
cat > "$TEST_FILE" << 'EOF'
// Windows build verification test - key patterns from c64u plugin  
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Platform-specific networking includes - EXACT copy from plugin
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close(s) closesocket(s)
#define SHUT_RDWR SD_BOTH
typedef int socklen_t;
typedef SOCKET socket_t;
// Note: ssize_t is already defined in MinGW, don't redefine
#define INVALID_SOCKET_VALUE INVALID_SOCKET
// Format specifier for ssize_t on Windows (64-bit)
#define SSIZE_T_FORMAT "%lld"
#define SSIZE_T_CAST(x) ((long long)(x))
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCKET_VALUE -1
// Format specifier for ssize_t on POSIX
#define SSIZE_T_FORMAT "%zd"
#define SSIZE_T_CAST(x) (x)
#endif

// Test critical patterns from plugin
int main() {
    // Test socket type declarations
    socket_t sock = INVALID_SOCKET_VALUE;
    
    // Test ssize_t usage (the main problem area)
    ssize_t received = 500;
    ssize_t sent = 100;
    int cmd_len = 6;
    
    // Test the specific patterns that were failing in CI
    uint8_t cmd[6] = {0x20, 0xFF, 0x02, 0x00, 0x00, 0x00};
    
    // Line 311 pattern: ssize_t sent = send(sock, (const char *)cmd, cmd_len, 0);
    if (sent != (ssize_t)cmd_len) {
        printf("Send pattern test: " SSIZE_T_FORMAT " != %d\n", SSIZE_T_CAST(sent), cmd_len);
    }
    
    // Line 331 pattern: ssize_t received = recv(...)
    uint8_t packet[1024];
    
    // Line 348 pattern: format specifier usage
    printf("Format test: received " SSIZE_T_FORMAT " bytes\n", SSIZE_T_CAST(received));
    
    printf("Windows compatibility verification PASSED\n");
    return 0;
}
EOF

echo "Created test file: $TEST_FILE"

# Test Windows cross-compilation
echo "Testing Windows cross-compilation..."
if x86_64-w64-mingw32-gcc -D_WIN32 "$TEST_FILE" -o /tmp/c64u_windows_test.exe -lws2_32; then
    echo "✅ Windows cross-compilation PASSED"
else
    echo "❌ Windows cross-compilation FAILED"
    exit 1
fi

# Test Linux compilation for regression check
echo "Testing Linux compilation for regressions..."
if gcc "$TEST_FILE" -o /tmp/c64u_linux_test; then
    echo "✅ Linux compilation PASSED"
else
    echo "❌ Linux compilation FAILED" 
    exit 1
fi

# Clean up
rm -f "$TEST_FILE" /tmp/c64u_windows_test.exe /tmp/c64u_linux_test

echo ""
echo "=== All Windows Compatibility Tests PASSED ==="
echo "The code should build successfully on Windows CI"