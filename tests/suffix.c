/*

  Copyright (c) 2017 Martin Sustrik

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

#include <string.h>

#include "assert.h"
#include "../libdill.h"

coroutine void client(void) {
    struct ipaddr addr;
    int cs, rc = ipaddr_remote(&addr, "127.0.0.1", 5555, 0, -1);
    errno_assert(rc == 0);
    int s = tcp_connect(&addr, -1);
    errno_assert(s >= 0);

    cs = suffix_attach(s, "\r\n", 2);
    errno_assert(cs >= 0);
    rc = msend(cs, "ABC", 3, -1);
    errno_assert(rc == 0);
    char buf[3];
    ssize_t sz = mrecv(cs, buf, 3, -1);
    errno_assert(sz == 3);
    assert(buf[0] == 'G' && buf[1] == 'H' && buf[2] == 'I');
    rc = msend(cs, "DEF", 3, -1);
    errno_assert(rc == 0);
    s = suffix_detach(cs, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

int main() {
    struct ipaddr addr;
    int rc = ipaddr_local(&addr, NULL, 5555, 0);
    errno_assert(rc == 0);
    int ls = tcp_listen(&addr, 10);
    errno_assert(ls >= 0);
    int clh = go(client());
    int as = tcp_accept(ls, NULL, -1);
    errno_assert(as >= 0);

    int cs = suffix_attach(as, "\r\n", 2);
    errno_assert(cs >= 0);
    char buf[16];
    ssize_t sz = mrecv(cs, buf, sizeof(buf), -1);
    errno_assert(sz == 3);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    rc = msend(cs, "GHI", 3, -1);
    errno_assert(rc == 0);
    sz = mrecv(cs, buf, sizeof(buf), -1);
    errno_assert(sz == 3);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    int ts = suffix_detach(cs, -1);
    errno_assert(ts >= 0);
    rc = hclose(ts);
    errno_assert(rc == 0);

    int h[2];
    rc = ipc_pair(h);
    errno_assert(rc == 0);
    int s0 = suffix_attach(h[0], "1234567890", 10);
    errno_assert(s0 >= 0);
    struct suffix_storage mem;
    int s1 = suffix_attach_mem(h[1], "1234567890", 10, &mem);
    errno_assert(s1 >= 0);
    rc = msend(s0, "First", 5, -1);
    errno_assert(rc == 0);
    rc = msend(s0, "Second", 6, -1);
    errno_assert(rc == 0);
    rc = msend(s0, "Third", 5, -1);
    errno_assert(rc == 0);
    sz = mrecv(s1, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 5 && memcmp(buf, "First", 5) == 0);
    struct iolist iol = {NULL, SIZE_MAX, NULL, 0};
    sz = mrecvl(s1, &iol, &iol, -1);
    errno_assert(sz >= 0);
    sz = mrecv(s1, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 5 && memcmp(buf, "Third", 5) == 0);
    /* A message received without an iolist is dropped. */
    rc = msend(s0, "Fourth", 6, -1);
    errno_assert(rc == 0);
    sz = mrecvl(s1, NULL, NULL, -1);
    errno_assert(sz == 6);
    rc = msend(s1, "Red", 3, -1);
    errno_assert(rc == 0);
    rc = msend(s1, "Blue", 4, -1);
    errno_assert(rc == 0);
    int ts1 = suffix_detach(s1, -1);
    errno_assert(ts1 >= 0);
    sz = mrecv(s0, buf, sizeof(buf), -1);
    errno_assert(sz == 3 && memcmp(buf, "Red", 3) == 0);
    sz = mrecv(s0, buf, sizeof(buf), -1);
    errno_assert(sz == 4 && memcmp(buf, "Blue", 4) == 0);
    int ts0 = suffix_detach(s0, -1);
    errno_assert(ts0 >= 0);
    rc = hclose(ts1);
    errno_assert(rc == 0);
    rc = hclose(ts0);
    errno_assert(rc == 0);
    rc = bundle_wait(clh, -1);
    errno_assert(rc == 0);
    rc = hclose(clh);
    errno_assert(rc == 0);
    rc = hclose(ls);
    errno_assert(rc == 0);

    /* A message is spread over all the buffers of an iolist. */
    rc = ipc_pair(h);
    errno_assert(rc == 0);
    s0 = suffix_attach(h[0], "\r\n", 2);
    errno_assert(s0 >= 0);
    s1 = suffix_attach(h[1], "\r\n", 2);
    errno_assert(s1 >= 0);
    rc = msend(s0, "ABCDE", 5, -1);
    errno_assert(rc == 0);
    char head[2];
    char tail[4];
    struct iolist iol_tail = {tail, sizeof(tail), NULL, 0};
    struct iolist iol_head = {head, sizeof(head), &iol_tail, 0};
    sz = mrecvl(s1, &iol_head, &iol_tail, -1);
    errno_assert(sz == 5);
    assert(memcmp(head, "AB", 2) == 0 && memcmp(tail, "CDE", 3) == 0);

    /* A buffer-less iolist item skips as many bytes as its length. */
    rc = msend(s0, "FGHIJ", 5, -1);
    errno_assert(rc == 0);
    struct iolist iol_skip = {NULL, 2, &iol_tail, 0};
    sz = mrecvl(s1, &iol_skip, &iol_tail, -1);
    errno_assert(sz == 5);
    assert(memcmp(tail, "HIJ", 3) == 0);

    /* A message longer than the buffer is rejected. */
    rc = msend(s0, "KLMNO", 5, -1);
    errno_assert(rc == 0);
    sz = mrecv(s1, buf, 4, -1);
    errno_assert(sz == -1 && errno == EMSGSIZE);
    sz = mrecv(s1, buf, sizeof(buf), -1);
    errno_assert(sz == -1 && errno == ECONNRESET);
    rc = hclose(s1);
    errno_assert(rc == 0);
    rc = hclose(s0);
    errno_assert(rc == 0);

    return 0;
}

