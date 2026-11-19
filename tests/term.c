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

#include <string.h>

#include "assert.h"
#include "../libdill.h"

coroutine void client(int s) {
    s = suffix_attach(s, "\r\n", 2);
    errno_assert(s >= 0);
    s = term_attach(s, "STOP", 4);
    errno_assert(s >= 0);
    int rc = msend(s, "ABC", 3, -1);
    errno_assert(s >= 0);
    char buf[16];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 3);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    s = term_detach(s, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(s >= 0);
} 

int main(void) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int cr = go(client(p[0]));
    errno_assert(cr >= 0);
    int s = suffix_attach(p[1], "\r\n", 2);
    errno_assert(s >= 0);
    s = term_attach(s, "STOP", 4);
    errno_assert(s >= 0);
    char buf[16];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 3);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    rc = msend(s, "DEF", 3, -1);
    errno_assert(rc == 0);
    sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz == -1 && errno == EPIPE);
    s = term_detach(s, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(s >= 0);
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    /* The terminator must fit into the socket and the storage must exist. */
    rc = ipc_pair(p);
    errno_assert(rc == 0);
    s = suffix_attach(p[0], "\r\n", 2);
    errno_assert(s >= 0);
    char terminator[33] = {0};
    s = term_attach(s, terminator, sizeof(terminator));
    errno_assert(s == -1 && errno == EINVAL);
    s = suffix_attach(p[1], "\r\n", 2);
    errno_assert(s >= 0);
    s = term_attach_mem(s, terminator, 1, NULL);
    errno_assert(s == -1 && errno == EINVAL);

    /* Detaching fails if the peer closes the connection without sending
       the terminator. */
    rc = ipc_pair(p);
    errno_assert(rc == 0);
    rc = ipc_done(p[0], -1);
    errno_assert(rc == 0);
    s = suffix_attach(p[1], "\r\n", 2);
    errno_assert(s >= 0);
    s = term_attach(s, "STOP", 4);
    errno_assert(s >= 0);
    s = term_detach(s, -1);
    errno_assert(s == -1 && errno == EPIPE);
    rc = hclose(p[0]);
    errno_assert(rc == 0);

    /* A message is rejected if it does not fit into a buffer shorter than
       the terminator. Shorter messages still fit or can be dropped. */
    rc = ipc_pair(p);
    errno_assert(rc == 0);
    p[0] = prefix_attach(p[0], 2, 0);
    errno_assert(p[0] >= 0);
    s = prefix_attach(p[1], 2, 0);
    errno_assert(s >= 0);
    s = term_attach(s, "STOP", 4);
    errno_assert(s >= 0);
    rc = msend(p[0], "ABC", 3, -1);
    errno_assert(rc == 0);
    sz = mrecv(s, buf, 2, -1);
    errno_assert(sz == -1 && errno == EMSGSIZE);
    rc = msend(p[0], "DE", 2, -1);
    errno_assert(rc == 0);
    sz = mrecv(s, buf, 2, -1);
    errno_assert(sz == 2);
    assert(buf[0] == 'D' && buf[1] == 'E');
    rc = msend(p[0], "FG", 2, -1);
    errno_assert(rc == 0);
    sz = mrecvl(s, NULL, NULL, -1);
    errno_assert(sz == 2);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(p[0]);
    errno_assert(rc == 0);

    /* Receive a message longer than the terminator from a protocol that
       hands the iolist down to a bytestream. */
    rc = ipc_pair(p);
    errno_assert(rc == 0);
    p[0] = prefix_attach(p[0], 2, 0);
    errno_assert(p[0] >= 0);
    s = prefix_attach(p[1], 2, 0);
    errno_assert(s >= 0);
    s = term_attach(s, "STOP", 4);
    errno_assert(s >= 0);
    rc = msend(p[0], "ABCDEF", 6, -1);
    errno_assert(rc == 0);
    sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz == 6);
    assert(memcmp(buf, "ABCDEF", 6) == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(p[0]);
    errno_assert(rc == 0);

    return 0;
}

