/*
 * Test program for enhanced DNS resolution functions
 * 
 * This tests the core DNS resolution logic without OBS dependencies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <resolv.h>
#include <arpa/nameser.h>

// Mock OBS logging functions for testing
#define LOG_DEBUG 0
#define LOG_INFO 100
#define LOG_WARNING 200
#define LOG_ERROR 300

void obs_log(int level, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    const char *level_str;
    switch (level) {
    case LOG_INFO:
        level_str = "INFO";
        break;
    case LOG_WARNING:
        level_str = "WARNING";
        break;
    case LOG_ERROR:
        level_str = "ERROR";
        break;
    default:
        level_str = "DEBUG";
        break;
    }

    printf("[%s] ", level_str);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

// Copy the direct DNS resolution functions from our implementation
bool resolve_hostname_direct_dns(const char *hostname, const char *dns_server_ip, char *ip_buffer, size_t buffer_size)
{
    if (!hostname || !dns_server_ip || !ip_buffer || buffer_size < 16) {
        return false;
    }

    obs_log(LOG_DEBUG, "[C64U] Trying direct DNS query: %s via %s", hostname, dns_server_ip);

    // Initialize resolver
    struct __res_state res_state;
    memset(&res_state, 0, sizeof(res_state));

    if (res_ninit(&res_state) != 0) {
        obs_log(LOG_WARNING, "[C64U] Failed to initialize resolver");
        return false;
    }

    // Set custom DNS server
    struct sockaddr_in dns_addr;
    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(53);

    if (inet_pton(AF_INET, dns_server_ip, &dns_addr.sin_addr) != 1) {
        obs_log(LOG_WARNING, "[C64U] Invalid DNS server IP: %s", dns_server_ip);
        res_nclose(&res_state);
        return false;
    }

    // Configure resolver to use our DNS server
    res_state.nscount = 1;
    res_state.nsaddr_list[0] = dns_addr;
    res_state.options |= RES_RECURSE;

    // Prepare query buffer
    unsigned char answer[4096];
    int answer_len = res_nquery(&res_state, hostname, ns_c_in, ns_t_a, answer, sizeof(answer));

    if (answer_len < 0) {
        obs_log(LOG_DEBUG, "[C64U] DNS query failed for %s via %s", hostname, dns_server_ip);
        res_nclose(&res_state);
        return false;
    }

    // Parse the response
    ns_msg msg;
    if (ns_initparse(answer, answer_len, &msg) < 0) {
        obs_log(LOG_WARNING, "[C64U] Failed to parse DNS response");
        res_nclose(&res_state);
        return false;
    }

    // Look for A records in the answer section
    int answer_count = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < answer_count; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) {
            continue;
        }

        if (ns_rr_type(rr) == ns_t_a && ns_rr_rdlen(rr) == 4) {
            // Found an A record - extract the IP address
            const unsigned char *rdata = ns_rr_rdata(rr);
            if (inet_ntop(AF_INET, rdata, ip_buffer, buffer_size) != NULL) {
                obs_log(LOG_INFO, "[C64U] Direct DNS resolved '%s' to %s via %s", hostname, ip_buffer, dns_server_ip);
                res_nclose(&res_state);
                return true;
            }
        }
    }

    obs_log(LOG_DEBUG, "[C64U] No A record found in DNS response from %s", dns_server_ip);
    res_nclose(&res_state);
    return false;
}

bool resolve_hostname_with_fallback_dns(const char *hostname, const char *custom_dns_server, char *ip_buffer,
                                        size_t buffer_size)
{
    if (!hostname || !ip_buffer || buffer_size < 16) {
        return false;
    }

    // Try custom DNS server first if provided
    if (custom_dns_server) {
        obs_log(LOG_DEBUG, "[C64U] Trying custom DNS server: %s", custom_dns_server);
        if (resolve_hostname_direct_dns(hostname, custom_dns_server, ip_buffer, buffer_size)) {
            return true;
        }
    }

    // Try common router/gateway IPs as DNS servers
    const char *common_dns_servers[] = {"192.168.1.1", // Most common home router
                                        "192.168.0.1", // Alternative common router
                                        "10.0.0.1",    // Common corporate/cable modem
                                        "172.16.0.1",  // Private network range
                                        "192.168.2.1", // Some routers use this
                                        "10.1.1.1",    // Alternative corporate
                                        NULL};

    obs_log(LOG_DEBUG, "[C64U] Trying common router DNS servers for fallback");

    for (int i = 0; common_dns_servers[i] != NULL; i++) {
        if (resolve_hostname_direct_dns(hostname, common_dns_servers[i], ip_buffer, buffer_size)) {
            return true;
        }
    }

    obs_log(LOG_DEBUG, "[C64U] All direct DNS attempts failed for: %s", hostname);
    return false;
}

// Simple hostname resolution test functions
bool test_system_dns(const char *hostname, char *ip_buffer, size_t buffer_size)
{
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname, NULL, &hints, &result);
    if (status == 0 && result != NULL) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)result->ai_addr;
        if (inet_ntop(AF_INET, &addr_in->sin_addr, ip_buffer, buffer_size) != NULL) {
            freeaddrinfo(result);
            return true;
        }
    }

    if (result) {
        freeaddrinfo(result);
    }
    return false;
}

bool test_ip_passthrough(const char *hostname, char *ip_buffer, size_t buffer_size)
{
    struct sockaddr_in sa;
    if (inet_pton(AF_INET, hostname, &sa.sin_addr) == 1) {
        strncpy(ip_buffer, hostname, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    printf("Enhanced DNS Resolution Test\n");
    printf("===========================\n\n");

    char ip_buffer[64];
    const char *hostname = (argc > 1) ? argv[1] : "c64u";
    const char *custom_dns = (argc > 2) ? argv[2] : NULL;

    printf("Testing hostname resolution for: %s\n", hostname);
    if (custom_dns) {
        printf("Using custom DNS server: %s\n", custom_dns);
    }
    printf("\n");

    // Test 1: System DNS resolution
    printf("1. Testing system DNS resolution:\n");
    if (test_system_dns(hostname, ip_buffer, sizeof(ip_buffer))) {
        printf("   SUCCESS: %s -> %s\n", hostname, ip_buffer);
    } else {
        printf("   FAILED: System DNS could not resolve %s\n", hostname);
    }
    printf("\n");

    // Test 2: Direct DNS resolution with custom server (if provided)
    if (custom_dns) {
        printf("2. Testing direct DNS with custom server (%s):\n", custom_dns);
        if (resolve_hostname_direct_dns(hostname, custom_dns, ip_buffer, sizeof(ip_buffer))) {
            printf("   SUCCESS: %s -> %s\n", hostname, ip_buffer);
        } else {
            printf("   FAILED: Could not resolve %s via %s\n", hostname, custom_dns);
        }
        printf("\n");
    }

    // Test 3: Fallback DNS resolution (tries common routers)
    printf("%d. Testing fallback DNS resolution:\n", custom_dns ? 3 : 2);
    if (resolve_hostname_with_fallback_dns(hostname, custom_dns, ip_buffer, sizeof(ip_buffer))) {
        printf("   SUCCESS: %s -> %s\n", hostname, ip_buffer);
    } else {
        printf("   FAILED: Fallback DNS could not resolve %s\n", hostname);
    }
    printf("\n");

    // Test 4: IP address passthrough
    printf("%d. Testing IP address passthrough:\n", custom_dns ? 4 : 3);
    const char *test_ip = "192.168.1.100";
    if (test_ip_passthrough(test_ip, ip_buffer, sizeof(ip_buffer))) {
        printf("   SUCCESS: %s -> %s (passthrough)\n", test_ip, ip_buffer);
    } else {
        printf("   FAILED: IP passthrough failed for %s\n", test_ip);
    }
    printf("\n");

    printf("Testing complete.\n");
    printf("\nTo test with a specific DNS server, run:\n");
    printf("  %s %s 192.168.1.1\n", argv[0], hostname);
    printf("\nTo test a different hostname, run:\n");
    printf("  %s mydevice\n", argv[0]);

    return 0;
}