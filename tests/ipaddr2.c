/*

  Copyright (c) 2026 Jean-Francois Smigielski

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom
  the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.

*/

/* Tests of ipaddr.c in situations that depend on the system it runs on.
   ipaddr.c is compiled into this program with the system facilities it relies
   on replaced by mocks, so that each test decides what the system does. */

#define dns_resconf_local mock_resconf_local
#define dns_hosts_local mock_hosts_local
#define dns_hints_local mock_hints_local
#define dns_res_open mock_res_open
#define dns_res_close mock_res_close
#define dns_ai_open mock_ai_open
#define dns_ai_nextent mock_ai_nextent
#define dns_ai_pollfd mock_ai_pollfd
#define dns_ai_events mock_ai_events
#define dns_ai_close mock_ai_close
#define getifaddrs mock_getifaddrs
#define freeifaddrs mock_freeifaddrs

#include "../ipaddr.c"
#include "assert.h"

/* ipaddr.c never looks into the objects of the DNS resolver, so the mocks
   hand out pointers to this one. */
static int mock_object;

struct dns_resolv_conf *mock_resconf_local(int *error) {
    return (struct dns_resolv_conf*)&mock_object;
}

struct dns_hosts *mock_hosts_local(int *error) {
    return (struct dns_hosts*)&mock_object;
}

struct dns_hints *mock_hints_local(struct dns_resolv_conf *conf, int *error) {
    return (struct dns_hints*)&mock_object;
}

struct dns_resolver *mock_res_open(struct dns_resolv_conf *conf,
      struct dns_hosts *hosts, struct dns_hints *hints, struct dns_cache *cache,
      const struct dns_options *opts, int *error) {
    return (struct dns_resolver*)&mock_object;
}

void mock_res_close(struct dns_resolver *resolver) {
}

struct dns_addrinfo *mock_ai_open(const char *host, const char *serv,
      enum dns_type qtype, const struct addrinfo *hints,
      struct dns_resolver *resolver, int *error) {
    return (struct dns_addrinfo*)&mock_object;
}

void mock_ai_close(struct dns_addrinfo *ai) {
}

/* No name has any address. */
int mock_ai_nextent(struct addrinfo **ent, struct dns_addrinfo *ai) {
    *ent = NULL;
    return ENOENT;
}

int mock_ai_pollfd(struct dns_addrinfo *ai) {
    return -1;
}

int mock_ai_events(struct dns_addrinfo *ai) {
    return 0;
}

#if !defined __sun

/* The network interfaces that getifaddrs() reports. */
static struct ifaddrs *mock_ifaces;

int mock_getifaddrs(struct ifaddrs **ifaces) {
    *ifaces = mock_ifaces;
    return 0;
}

void mock_freeifaddrs(struct ifaddrs *ifaces) {
}

/* Makes "dill0" the only network interface, with the addresses given as
   a NULL-terminated array of literals. */
static void set_interface(const char **literals) {
    static struct ifaddrs ifaces[4];
    static struct dill_ipaddr addrs[4];
    memset(ifaces, 0, sizeof(ifaces));
    int i;
    for(i = 0; literals[i]; i++) {
        assert(i < 4);
        int rc = dill_ipaddr_local(&addrs[i], literals[i], 0, 0);
        errno_assert(rc == 0);
        ifaces[i].ifa_name = "dill0";
        ifaces[i].ifa_addr = (struct sockaddr*)&addrs[i];
        if(i > 0) ifaces[i - 1].ifa_next = &ifaces[i];
    }
    mock_ifaces = ifaces;
}

#endif

int main(void) {

#if !defined __sun
    /* An interface may have several addresses of the same family. The first
       one is the address of the interface. */
    struct dill_ipaddr addr;
    char buf[DILL_IPADDR_MAXSTRLEN];
    const char *ipv4_twice[] = {"10.0.0.1", "10.0.0.2", "fd00::1", NULL};
    set_interface(ipv4_twice);
    int rc = dill_ipaddr_local(&addr, "dill0", 5555, DILL_IPADDR_IPV4);
    errno_assert(rc == 0);
    assert(strcmp(dill_ipaddr_str(&addr, buf), "10.0.0.1") == 0);
    assert(dill_ipaddr_port(&addr) == 5555);
    const char *ipv6_twice[] = {"fd00::1", "fd00::2", "10.0.0.1", NULL};
    set_interface(ipv6_twice);
    rc = dill_ipaddr_local(&addr, "dill0", 5555, DILL_IPADDR_IPV6);
    errno_assert(rc == 0);
    assert(strcmp(dill_ipaddr_str(&addr, buf), "fd00::1") == 0);
    assert(dill_ipaddr_port(&addr) == 5555);
#endif

    return 0;
}

