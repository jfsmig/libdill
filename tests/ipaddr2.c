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

/* The number of DNS queries that are open. */
static int open_queries = 0;

struct dns_addrinfo *mock_ai_open(const char *host, const char *serv,
      enum dns_type qtype, const struct addrinfo *hints,
      struct dns_resolver *resolver, int *error) {
    open_queries++;
    return (struct dns_addrinfo*)&mock_object;
}

void mock_ai_close(struct dns_addrinfo *ai) {
    open_queries--;
}

/* What the successive calls to dns_ai_nextent() return. Zero stands for one
   more IPv4 address, any other value ends the query. */
static const int *nextent_results;

int mock_ai_nextent(struct addrinfo **ent, struct dns_addrinfo *ai) {
    *ent = NULL;
    int result = *nextent_results++;
    if(result != 0) return result;
    /* As the real function does, allocate the entry and its address in one
       block, to be freed by the caller. */
    struct {
        struct addrinfo ai;
        struct sockaddr_in addr;
    } *entry = calloc(1, sizeof(*entry));
    assert(entry);
    entry->addr.sin_family = AF_INET;
    entry->ai.ai_family = AF_INET;
    entry->ai.ai_addr = (struct sockaddr*)&entry->addr;
    *ent = &entry->ai;
    return 0;
}

int mock_ai_pollfd(struct dns_addrinfo *ai) {
    return -1;
}

int mock_ai_events(struct dns_addrinfo *ai) {
    return 0;
}

/* Resolves a name, given what dns_ai_nextent() returns while doing so. */
static int resolve(const int *results) {
    struct dill_ipaddr addrs[4];
    nextent_results = results;
    int rc = dill_ipaddr_remotes(addrs, 4, "www.example.org", 80,
        DILL_IPADDR_IPV4, -1);
    int err = errno;
    assert(open_queries == 0);
    errno = err;
    return rc;
}

int main(void) {

    /* The resolver returns the addresses one by one. */
    const int two_addresses[] = {0, 0, ENOENT};
    int rc = resolve(two_addresses);
    errno_assert(rc == 2);

    /* A name that does not exist has no address. That is not an error. */
    const int no_such_name[] = {DNS_ENONAME};
    rc = resolve(no_such_name);
    errno_assert(rc == 0);

    /* A system error, such as the name server being unreachable, makes the
       resolution fail, whether it occurs before the first address... */
    const int unreachable[] = {ECONNREFUSED};
    rc = resolve(unreachable);
    assert(rc == -1 && errno == ECONNREFUSED);

    /* ...or after it. */
    const int unreachable_later[] = {0, ENETUNREACH};
    rc = resolve(unreachable_later);
    assert(rc == -1 && errno == ENETUNREACH);

    return 0;
}

