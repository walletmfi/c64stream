/*
 * Test hostname resolution using system DNS on Linux/macOS
 *
 * This test verifies that we can resolve hostnames like 'c64u' to IP addresses
 * using the system's DNS configuration, which should work in OBS plugin context.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Low-level DNS resolution (Linux/macOS)
#include <resolv.h>
#include <arpa/nameser.h>
#endif

/**
 * Cross-platform hostname resolution test
 * Tests multiple approaches to ensure we find the best method for OBS plugin
 */

#ifndef _WIN32
/**
 * Method 1a: Low-level DNS resolution using system DNS servers
 */
int resolve_hostname_system_dns(const char *hostname, char *ip_buffer, size_t buffer_size)
{
    struct __res_state res;
    unsigned char answer[NS_PACKETSZ];
    ns_msg msg;
    ns_rr rr;

    printf("Testing system DNS resolution for: %s\n", hostname);

    // Initialize resolver
    if (res_ninit(&res) != 0) {
        perror("res_ninit failed");
        return -1;
    }

    // Check if we have system DNS servers
    if (res.nscount == 0) {
        fprintf(stderr, "No system DNS servers found\n");
        res_nclose(&res);
        return -1;
    }

    printf("Found %d system DNS servers:\n", res.nscount);
    for (int i = 0; i < res.nscount && i < MAXNS; i++) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&res.nsaddr_list[i];
        printf("  DNS[%d]: %s\n", i, inet_ntoa(sin->sin_addr));
    }

    // Perform DNS query for A record
    int len = res_nquery(&res, hostname, ns_c_in, ns_t_a, answer, sizeof(answer));
    if (len < 0) {
        fprintf(stderr, "System DNS query failed for %s (h_errno: %d)\n", hostname, h_errno);
        res_nclose(&res);
        return -1;
    }

    // Parse DNS response
    if (ns_initparse(answer, len, &msg) < 0) {
        perror("ns_initparse failed");
        res_nclose(&res);
        return -1;
    }

    int ancount = ns_msg_count(msg, ns_s_an);
    if (ancount == 0) {
        printf("No A record found for %s\n", hostname);
        res_nclose(&res);
        return -1;
    }

    // Get the first A record
    for (int i = 0; i < ancount; i++) {
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0)
            continue;

        if (ns_rr_type(rr) == ns_t_a && ns_rr_rdlen(rr) == 4) {
            struct in_addr addr;
            memcpy(&addr, ns_rr_rdata(rr), sizeof(addr));
            const char *ip_str = inet_ntoa(addr);

            printf("✅ System DNS: %s -> %s\n", hostname, ip_str);

            if (ip_buffer && buffer_size > strlen(ip_str)) {
                strcpy(ip_buffer, ip_str);
            }

            res_nclose(&res);
            return 0;
        }
    }

    res_nclose(&res);
    return -1;
}

/**
 * Method 1b: Direct DNS query to specific server (like dig @192.168.1.1)
 * This bypasses systemd-resolved and queries the router directly
 */
int resolve_hostname_direct_dns(const char *hostname, const char *dns_server, char *ip_buffer, size_t buffer_size)
{
    struct __res_state res;
    unsigned char answer[NS_PACKETSZ];
    ns_msg msg;
    ns_rr rr;

    printf("Testing direct DNS query to %s for: %s\n", dns_server, hostname);

    // Initialize resolver
    if (res_ninit(&res) != 0) {
        perror("res_ninit failed");
        return -1;
    }

    // Override DNS server with the router's DNS
    struct sockaddr_in dns_addr;
    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(53);

    if (inet_aton(dns_server, &dns_addr.sin_addr) == 0) {
        fprintf(stderr, "Invalid DNS server IP: %s\n", dns_server);
        res_nclose(&res);
        return -1;
    }

    // Clear existing DNS servers and set our custom one
    res.nscount = 1;
    memcpy(&res.nsaddr_list[0], &dns_addr, sizeof(dns_addr));

    printf("Using DNS server: %s\n", dns_server);

    // Perform DNS query for A record
    int len = res_nquery(&res, hostname, ns_c_in, ns_t_a, answer, sizeof(answer));
    if (len < 0) {
        fprintf(stderr, "Direct DNS query to %s failed for %s (h_errno: %d)\n", dns_server, hostname, h_errno);
        res_nclose(&res);
        return -1;
    }

    // Parse DNS response
    if (ns_initparse(answer, len, &msg) < 0) {
        perror("ns_initparse failed");
        res_nclose(&res);
        return -1;
    }

    int ancount = ns_msg_count(msg, ns_s_an);
    if (ancount == 0) {
        printf("No A record found for %s via %s\n", hostname, dns_server);
        res_nclose(&res);
        return -1;
    }

    // Get the first A record
    for (int i = 0; i < ancount; i++) {
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0)
            continue;

        if (ns_rr_type(rr) == ns_t_a && ns_rr_rdlen(rr) == 4) {
            struct in_addr addr;
            memcpy(&addr, ns_rr_rdata(rr), sizeof(addr));
            const char *ip_str = inet_ntoa(addr);

            printf("✅ Direct DNS via %s: %s -> %s\n", dns_server, hostname, ip_str);

            if (ip_buffer && buffer_size > strlen(ip_str)) {
                strcpy(ip_buffer, ip_str);
            }

            res_nclose(&res);
            return 0;
        }
    }

    res_nclose(&res);
    return -1;
}

/**
 * Method 2: Standard getaddrinfo() - more portable but may have issues in some environments
 */
int resolve_hostname_getaddrinfo(const char *hostname, char *ip_buffer, size_t buffer_size)
{
    struct addrinfo hints, *result;

    printf("Testing getaddrinfo() resolution for: %s\n", hostname);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname, NULL, &hints, &result);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(status));
        return -1;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)result->ai_addr;
    const char *ip_str = inet_ntoa(addr_in->sin_addr);

    printf("✅ getaddrinfo(): %s -> %s\n", hostname, ip_str);

    if (ip_buffer && buffer_size > strlen(ip_str)) {
        strcpy(ip_buffer, ip_str);
    }

    freeaddrinfo(result);
    return 0;
}

/**
 * Method 3: gethostbyname() - legacy but widely supported
 */
int resolve_hostname_gethostbyname(const char *hostname, char *ip_buffer, size_t buffer_size)
{
    printf("Testing gethostbyname() resolution for: %s\n", hostname);

    struct hostent *host_entry = gethostbyname(hostname);
    if (host_entry == NULL) {
        fprintf(stderr, "gethostbyname failed: h_errno=%d\n", h_errno);
        return -1;
    }

    struct in_addr addr;
    addr.s_addr = *((unsigned long *)host_entry->h_addr_list[0]);
    const char *ip_str = inet_ntoa(addr);

    printf("✅ gethostbyname(): %s -> %s\n", hostname, ip_str);

    if (ip_buffer && buffer_size > strlen(ip_str)) {
        strcpy(ip_buffer, ip_str);
    }

    return 0;
}
#endif

/**
 * Test if a string is already an IP address
 */
int is_ip_address(const char *str)
{
    struct sockaddr_in sa;
    return inet_aton(str, &sa.sin_addr) != 0;
}

/**
 * Comprehensive hostname resolution with fallback methods
 */
int resolve_hostname_comprehensive(const char *hostname, char *ip_buffer, size_t buffer_size)
{
    printf("\n=== Testing hostname resolution for: '%s' ===\n", hostname);

    // Check if it's already an IP address
    if (is_ip_address(hostname)) {
        printf("✅ Input is already an IP address: %s\n", hostname);
        if (ip_buffer && buffer_size > strlen(hostname)) {
            strcpy(ip_buffer, hostname);
        }
        return 0;
    }

#ifdef _WIN32
    printf("Windows hostname resolution not implemented in this test\n");
    return -1;
#else

    // Try Method 1a: System DNS resolution
    printf("\n--- Method 1a: System DNS resolution ---\n");
    if (resolve_hostname_system_dns(hostname, ip_buffer, buffer_size) == 0) {
        printf("✅ System DNS resolution succeeded\n");
        return 0;
    }

    // Try Method 1b: Direct router DNS (like dig @192.168.1.1)
    printf("\n--- Method 1b: Direct router DNS resolution ---\n");
    const char *router_dns_servers[] = {"192.168.1.1", // Common router IP
                                        "192.168.0.1", // Alternative router IP
                                        "10.0.0.1",    // Another common router IP
                                        NULL};

    for (int i = 0; router_dns_servers[i] != NULL; i++) {
        printf("Trying router DNS: %s\n", router_dns_servers[i]);
        if (resolve_hostname_direct_dns(hostname, router_dns_servers[i], ip_buffer, buffer_size) == 0) {
            printf("✅ Direct router DNS resolution succeeded via %s\n", router_dns_servers[i]);
            return 0;
        }
    }

    // Try Method 2: getaddrinfo()
    printf("\n--- Method 2: getaddrinfo() resolution ---\n");
    if (resolve_hostname_getaddrinfo(hostname, ip_buffer, buffer_size) == 0) {
        printf("✅ getaddrinfo() resolution succeeded\n");
        return 0;
    }

    // Try Method 3: gethostbyname()
    printf("\n--- Method 3: gethostbyname() resolution ---\n");
    if (resolve_hostname_gethostbyname(hostname, ip_buffer, buffer_size) == 0) {
        printf("✅ gethostbyname() resolution succeeded\n");
        return 0;
    }

    printf("❌ All hostname resolution methods failed\n");
    return -1;
#endif
}

int main(int argc, char **argv)
{
    printf("Hostname Resolution Test for OBS C64 Stream Plugin\n");
    printf("============================================\n");

    if (argc < 2) {
        printf("Usage: %s <hostname_or_ip> [hostname2] [...]\n", argv[0]);
        printf("Examples:\n");
        printf("  %s c64u\n", argv[0]);
        printf("  %s 192.168.1.13\n", argv[0]);
        printf("  %s localhost\n", argv[0]);
        printf("  %s c64u 192.168.1.13 localhost google.com\n", argv[0]);
        return 1;
    }

    int overall_success = 0;

    // Test each hostname provided
    for (int i = 1; i < argc; i++) {
        char ip_buffer[INET_ADDRSTRLEN];

        int result = resolve_hostname_comprehensive(argv[i], ip_buffer, sizeof(ip_buffer));

        if (result == 0) {
            printf("🎉 SUCCESS: %s resolved to %s\n", argv[i], ip_buffer);
            overall_success++;
        } else {
            printf("💥 FAILED: Could not resolve %s\n", argv[i]);
        }

        if (i < argc - 1) {
            printf("\n"
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "="
                   "\n");
        }
    }

    printf("\n🏁 SUMMARY: %d/%d hostnames resolved successfully\n", overall_success, argc - 1);

    return (overall_success == argc - 1) ? 0 : 1;
}
