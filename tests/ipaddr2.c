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
#define dns_resconf_close mock_resconf_close
#define dns_hosts_local mock_hosts_local
#define dns_hosts_close mock_hosts_close
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

/* The errors that loading each part of the DNS configuration fails with.
   Zero makes it succeed. */
static int resconf_error = 0;
static int hosts_error = 0;
static int hints_error = 0;

/* The number of parts of the DNS configuration that are loaded. */
static int loaded_parts = 0;

struct dns_resolv_conf *mock_resconf_local(int *error) {
    if(resconf_error) {
        *error = resconf_error;
        return NULL;
    }
    loaded_parts++;
    return (struct dns_resolv_conf*)&mock_object;
}

void mock_resconf_close(struct dns_resolv_conf *conf) {
    loaded_parts--;
}

struct dns_hosts *mock_hosts_local(int *error) {
    if(hosts_error) {
        *error = hosts_error;
        return NULL;
    }
    loaded_parts++;
    return (struct dns_hosts*)&mock_object;
}

void mock_hosts_close(struct dns_hosts *hosts) {
    loaded_parts--;
}

struct dns_hints *mock_hints_local(struct dns_resolv_conf *conf, int *error) {
    if(hints_error) {
        *error = hints_error;
        return NULL;
    }
    loaded_parts++;
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

static int resolve(void) {
    struct dill_ipaddr addr;
    return dill_ipaddr_remotes(&addr, 1, "www.example.org", 80,
        DILL_IPADDR_IPV4, -1);
}

int main(void) {

    /* The DNS configuration cannot be loaded. Whichever part is missing, it is
       an error, reported with its cause, and no other part is kept. */
    resconf_error = EACCES;
    int rc = resolve();
    assert(rc == -1 && errno == EACCES);
    assert(loaded_parts == 0);
    resconf_error = 0;
    hosts_error = ENOENT;
    rc = resolve();
    assert(rc == -1 && errno == ENOENT);
    assert(loaded_parts == 0);
    hosts_error = 0;
    hints_error = ENOMEM;
    rc = resolve();
    assert(rc == -1 && errno == ENOMEM);
    assert(loaded_parts == 0);
    hints_error = 0;

    /* Once it can be loaded, the configuration is loaded once and for all. */
    rc = resolve();
    errno_assert(rc == 0);
    assert(loaded_parts == 3);
    rc = resolve();
    errno_assert(rc == 0);
    assert(loaded_parts == 3);

    return 0;
}

