/*

  Copyright (c) 2018 Martin Sustrik

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

#include <errno.h>

#define DILL_DISABLE_RAW_NAMES
#include "libdill.h"
#include "utils.h"

static dill_coroutine void dill_happyeyeballs_dnsquery(const char *name,
      int port, int mode, int ch, int *err) {
    /* Do the DNS query. Let's be reasonable and limit the number of addresses
       to 10 per IP version. */
    struct dill_ipaddr addrs[10];
    int count = dill_ipaddr_remotes(addrs, 10, name, port, mode, -1);
    if(dill_slow(count < 0)) *err = errno;
    if(count > 0) {
        /* Send the results of the query to the main fuction. */
        int i;
        for(i = 0; i != count; ++i) {
           int rc = dill_chsend(ch, &addrs[i], sizeof(struct dill_ipaddr), -1);
           if(dill_slow(rc < 0 && errno == ECANCELED)) return;
           dill_assert(rc == 0);
        }
    }
    /* chdone is deliberately not being called here so that sorter doesn't
       have to deal with closed channels. Instead we'll send 0.0.0.0 address
       to mark the end of the results. */
    struct dill_ipaddr addr;
    int rc = dill_ipaddr_local(&addr, "0.0.0.0", 0, DILL_IPADDR_IPV4);
    dill_assert(rc == 0);
    rc = dill_chsend(ch, &addr, sizeof(struct dill_ipaddr), -1);
    dill_assert(rc == 0 || errno == ECANCELED);
}

static dill_coroutine void dill_happyeyeballs_attempt(struct dill_ipaddr addr,
      int ch, int *err) {
    int conn = dill_tcp_connect(&addr, -1);
    if(dill_slow(conn < 0)) {*err = errno; return;}
    int rc = dill_chsend(ch, &conn, sizeof(int), -1);
    if(dill_slow(rc < 0 && errno == ECANCELED)) {
        rc = dill_hclose(conn);
        dill_assert(rc == 0);
        return;
    }
    dill_assert(rc == 0);
}

/* Tells dill_happyeyeballs_connect that there will be no connection. The error
   travels as a negative number through the channel meant for the connection. */
static void dill_happyeyeballs_fail(int ch, int err) {
    dill_assert(err > 0);
    int res = -err;
    int rc = dill_chsend(ch, &res, sizeof(int), -1);
    dill_assert(rc == 0 || errno == ECANCELED);
}

static dill_coroutine void dill_happyeyeballs_coordinator(
      const char *name, int port, int ch) {
    struct dill_ipaddr nulladdr;
    int rc = dill_ipaddr_local(&nulladdr, "0.0.0.0", 0, DILL_IPADDR_IPV4);
    dill_assert(rc == 0);
    /* The error to report if no connection can be made: the cause of the most
       recent failure of a DNS query or a connection attempt. If nothing
       fails, the name has no address at all. It is kept apart from err, the
       error of the coordinator itself, because the coroutines that are
       canceled while the coordinator cleans up record ECANCELED in it. */
    int lasterr = EADDRNOTAVAIL;
    int err;
    /* According to the RFC, IPv4 and IPv6 DNS queries should be done in
       parallel. Create two coroutines and two channels to pass
       the addresses from them. */
    int chipv6[2];
    struct dill_chstorage chipv6_storage;
    rc = dill_chmake_mem(&chipv6_storage, chipv6);
    if(dill_slow(rc < 0)) {err = errno; goto error1;}
    int chipv4[2];
    struct dill_chstorage chipv4_storage;
    rc = dill_chmake_mem(&chipv4_storage, chipv4);
    if(dill_slow(rc < 0)) {err = errno; goto error2;}
    struct dill_bundle_storage bndl_storage;
    int bndl = dill_bundle_mem(&bndl_storage);
    if(dill_slow(bndl < 0)) {err = errno; goto error3;}
    rc = dill_bundle_go(bndl, dill_happyeyeballs_dnsquery(name, port,
        DILL_IPADDR_IPV6, chipv6[1], &lasterr));
    if(dill_slow(rc < 0)) {err = errno; goto error4;}
    rc = dill_bundle_go(bndl, dill_happyeyeballs_dnsquery(name, port,
        DILL_IPADDR_IPV4, chipv4[1], &lasterr));
    if(dill_slow(rc < 0)) {err = errno; goto error4;}
    /* RFC says to wait for 50ms for IPv6 result irrespective of whether
       IPv4 address arrives. */
    struct dill_ipaddr addr;
    struct dill_chclause cls[2] = {
        {DILL_CHRECV, chipv6[0], &addr, sizeof(struct dill_ipaddr)},
        {DILL_CHRECV, chipv4[0], &addr, sizeof(struct dill_ipaddr)}
    };
    rc = dill_chrecv(chipv6[0], &addr, sizeof(struct dill_ipaddr),
        dill_now() + 50);
    if(dill_slow(rc < 0 && errno == ECANCELED)) goto cancel;
    int idx;
    int unresolved = 2;
    if(dill_fast(rc == 0)) {
        idx = 0;
        goto use_address;
    }
    dill_assert(errno == ETIMEDOUT);
    /* From now we can use any address. */
    while(1) {
        idx = dill_choose(cls, 2, -1);
        if(dill_slow(idx < 0 && errno == ECANCELED)) goto cancel;
        dill_assert(idx >= 0 && errno == 0);
use_address:
        /* A 0.0.0.0 address is not to be used. It marks the end of the results
           of one of the two DNS queries. */
        if(dill_ipaddr_equal(&addr, &nulladdr, 0)) {
            unresolved--;
            if(dill_slow(!unresolved)) break;
            continue;
        }
        /* Launch the connect attempt. */
        rc = dill_bundle_go(bndl, dill_happyeyeballs_attempt(addr, ch, &lasterr));
        if(dill_slow(rc < 0)) {err = errno; goto error4;}
        /* Alternate between IPv4 and IPv6 addresses. */
        if(idx == 0) {
            int tmp = cls[0].ch;
            cls[0].ch = cls[1].ch;
            cls[1].ch = tmp;
        }
        /* Wait for 300ms before launching next connection attempt. */
        rc = dill_msleep(dill_now() + 300);
        if(dill_slow(rc < 0 && errno == ECANCELED)) goto cancel;
        dill_assert(rc == 0);
    }
    /* No more addresses are going to come. If none of the attempts still in
       progress succeeds, there is nothing left to try. */
    rc = dill_bundle_wait(bndl, -1);
    if(dill_slow(rc < 0 && errno == ECANCELED)) goto cancel;
    dill_assert(rc == 0);
    err = lasterr;
    goto error4;
cancel:
    err = ECANCELED;
error4:
    rc = dill_hclose(bndl);
    dill_assert(rc == 0);
error3:
    rc = dill_hclose(chipv4[0]);
    dill_assert(rc == 0);
    rc = dill_hclose(chipv4[1]);
    dill_assert(rc == 0);
error2:
    rc = dill_hclose(chipv6[0]);
    dill_assert(rc == 0);
    rc = dill_hclose(chipv6[1]);
    dill_assert(rc == 0);
error1:
    /* When canceled, nobody is waiting for the outcome any more. */
    if(err != ECANCELED) dill_happyeyeballs_fail(ch, err);
}

int dill_happyeyeballs_connect(const char *name, int port, int64_t deadline) {
    int res = -1;
    int err = 0;
    if(dill_slow(!name || port <= 0)) {err = EINVAL; goto exit1;}
    int chconns[2];
    struct dill_chstorage chconns_storage;
    int rc = dill_chmake_mem(&chconns_storage, chconns);
    if(dill_slow(rc < 0)) {err = errno; goto exit1;}
    int coord = dill_go(dill_happyeyeballs_coordinator(name, port, chconns[1]));
    if(dill_slow(coord < 0)) {err = errno; goto exit2;}
    int conn;
    rc = dill_chrecv(chconns[0], &conn, sizeof(int), deadline);
    if(dill_slow(rc < 0)) {err = errno; goto exit3;}
    if(dill_slow(conn < 0)) {err = -conn; goto exit3;}
    res = conn;
exit3:
    rc = dill_hclose(coord);
    dill_assert(rc == 0);
exit2:
    rc = dill_hclose(chconns[0]);
    dill_assert(rc == 0);
    rc = dill_hclose(chconns[1]);
    dill_assert(rc == 0);
exit1:
    errno = err;
    return res;
}

