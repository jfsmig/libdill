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

/* Loading a part of the DNS configuration takes a while. Each time it starts,
   a letter naming the part is written to this pipe. */
static int loading[2];

static void load_slowly(char part) {
    ssize_t sz = write(loading[1], &part, 1);
    errno_assert(sz == 1);
    usleep(100000);
}

/* The number of times resolv.conf was loaded. */
static int resconf_loads = 0;

struct dns_resolv_conf *mock_resconf_local(int *error) {
    resconf_loads++;
    load_slowly('r');
    return (struct dns_resolv_conf*)&mock_object;
}

struct dns_hosts *mock_hosts_local(int *error) {
    load_slowly('h');
    return (struct dns_hosts*)&mock_object;
}

struct dns_hints *mock_hints_local(struct dns_resolv_conf *conf, int *error) {
    return (struct dns_hints*)&mock_object;
}

struct dns_resolver *mock_res_open(struct dns_resolv_conf *conf,
      struct dns_hosts *hosts, struct dns_hints *hints, struct dns_cache *cache,
      const struct dns_options *opts, int *error) {
    /* A query must never get a configuration that is only partly loaded. */
    assert(conf && hosts && hints);
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

#if defined DILL_THREADS

#include <pthread.h>

static void *lookup(void *arg) {
    struct dill_ipaddr addr;
    int rc = dill_ipaddr_remotes(&addr, 1, "www.example.org", 80,
        DILL_IPADDR_IPV4, -1);
    errno_assert(rc == 0);
    return NULL;
}

static void wait_for_loading(char part) {
    char started;
    do {
        ssize_t sz = read(loading[0], &started, 1);
        errno_assert(sz == 1);
    } while(started != part);
}

#endif

int main(void) {

#if defined DILL_THREADS
    /* Threads that do their first lookup at the same time share the DNS
       configuration. One of them loads it, once, and none uses it before it
       is complete. Here the second thread turns up while the first one loads
       resolv.conf, and the third one while it loads the hosts. */
    int rc = pipe(loading);
    errno_assert(rc == 0);
    pthread_t threads[3];
    rc = pthread_create(&threads[0], NULL, lookup, NULL);
    assert(rc == 0);
    wait_for_loading('r');
    rc = pthread_create(&threads[1], NULL, lookup, NULL);
    assert(rc == 0);
    wait_for_loading('h');
    rc = pthread_create(&threads[2], NULL, lookup, NULL);
    assert(rc == 0);
    int i;
    for(i = 0; i != 3; i++) {
        rc = pthread_join(threads[i], NULL);
        assert(rc == 0);
    }
    assert(resconf_loads == 1);
#endif

    return 0;
}

