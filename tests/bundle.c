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

#include <stdint.h>
#include <string.h>

#include "assert.h"
#include "../libdill.h"

coroutine void worker1(void) {
}

coroutine void worker2(void) {
    int rc = msleep(now() + 100000);
    assert(rc == -1 && errno == ECANCELED);
}

coroutine void worker3(int delay) {
    int rc = msleep(now() + delay);
    assert(rc == 0);
}

coroutine void worker4(int64_t deadline) {
    int rc = msleep(deadline);
    errno_assert(rc == 0);
}

coroutine void worker5(int ch) {
    int val;
    int rc = chrecv(ch, &val, sizeof(val), -1);
    errno_assert(rc == 0);
}

coroutine void waiter(int bndl) {
    int rc = bundle_wait(bndl, -1);
    errno_assert(rc == -1 && errno == ECANCELED);
}

coroutine void waiter_of_closed_bundle(int bndl) {
    int rc = bundle_wait(bndl, now() + 100);
    errno_assert(rc == -1 && errno == EPIPE);
}

/* Test closing a bundle while a coroutine waits for it. The waiter fails at
   once and leaves the bundle's storage alone from then on. */
static void test_close_while_waited_for(void) {
    struct bundle_storage mem;
    int bndl = bundle_mem(&mem);
    errno_assert(bndl >= 0);
    int rc = bundle_go(bndl, worker2());
    errno_assert(rc == 0);
    int cr = go(waiter_of_closed_bundle(bndl));
    errno_assert(cr >= 0);
    rc = hclose(bndl);
    errno_assert(rc == 0);
    memset(&mem, 0xaa, sizeof(mem));
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    size_t i;
    for(i = 0; i != sizeof(mem); ++i) assert(((uint8_t*)&mem)[i] == 0xaa);
    rc = hclose(cr);
    errno_assert(rc == 0);
}

int main(void) {
    int hndl1 = bundle();
    errno_assert(hndl1 >= 0);
    int rc = hclose(hndl1);
    errno_assert(rc == 0);

    /* Test that missing storage is reported through errno. */
    errno = 0;
    rc = bundle_mem(NULL);
    errno_assert(rc == -1 && errno == EINVAL);

    int hndl2 = bundle();
    errno_assert(hndl2 >= 0);
    rc = bundle_go(hndl2, worker1());
    errno_assert(rc == 0);
    rc = hclose(hndl2);
    errno_assert(rc == 0);

    int hndl3 = bundle();
    errno_assert(hndl3 >= 0);
    rc = bundle_go(hndl3, worker1());
    errno_assert(rc == 0);
    rc = msleep(now() + 100);
    errno_assert(rc == 0);
    rc = hclose(hndl3);
    errno_assert(rc == 0);

    int hndl4 = bundle();
    errno_assert(hndl4 >= 0);
    rc = bundle_go(hndl4, worker2());
    errno_assert(rc == 0);
    rc = msleep(now() + 100);
    errno_assert(rc == 0);
    rc = hclose(hndl4);
    errno_assert(rc == 0);

    int hndl5 = bundle();
    errno_assert(hndl5 >= 0);
    rc = bundle_go(hndl5, worker3(50));
    errno_assert(rc == 0);
    rc = bundle_go(hndl5, worker3(50));
    errno_assert(rc == 0);
    rc = msleep(now() + 100);
    errno_assert(rc == 0);
    rc = hclose(hndl5);
    errno_assert(rc == 0);

    int hndl6 = bundle();
    errno_assert(hndl6 >= 0);
    rc = bundle_go(hndl6, worker2());
    errno_assert(rc == 0);
    rc = bundle_go(hndl6, worker2());
    errno_assert(rc == 0);
    rc = msleep(now() + 100);
    errno_assert(rc == 0);
    rc = hclose(hndl6);
    errno_assert(rc == 0);

    int hndl7 = bundle();
    errno_assert(hndl7 >= 0);
    char stk[4096];
    rc = bundle_go_mem(hndl7, worker1(), stk, sizeof(stk));
    errno_assert(rc == 0);
    rc = hclose(hndl7);
    errno_assert(rc == 0);

    /* Test creating bundle via go() function. */
    int hndl8 = go(worker1());
    errno_assert(hndl8 >= 0);
    rc = bundle_go(hndl8, worker1());
    errno_assert(rc == 0);
    rc = msleep(now() + 100);
    errno_assert(rc == 0);
    rc = hclose(hndl8);
    errno_assert(rc == 0);

    /* Test waiting on empty bundle. */
    int hndl9 = bundle();
    errno_assert(hndl9 >= 0);
    rc = bundle_wait(hndl9, -1);
    errno_assert(rc == 0);
    rc = hclose(hndl9);
    errno_assert(rc == 0);

    /* Test waiting on bundle with one coroutine. */
    int hndl10 = go(worker3(50));
    errno_assert(hndl10 >= 0);
    rc = bundle_wait(hndl10, -1);
    errno_assert(rc == 0);
    rc = hclose(hndl10);
    errno_assert(rc == 0);

    /* Test waiting on bundle with two coroutines. */
    int hndl11 = bundle();
    errno_assert(hndl11 >= 0);
    rc = bundle_go(hndl11, worker3(50));
    errno_assert(rc == 0);
    rc = bundle_go(hndl11, worker3(100));
    errno_assert(rc == 0);
    rc = bundle_wait(hndl11, -1);
    errno_assert(rc == 0);
    rc = bundle_go(hndl11, worker1());
    errno_assert(rc == 0);
    rc = hclose(hndl11);
    errno_assert(rc == 0);

    /* Test waiting on bundle with timout. */
    int hndl12 = go(worker3(50));
    errno_assert(hndl12 >= 0);
    rc = bundle_wait(hndl12, now() + 1000);
    errno_assert(rc == 0);
    rc = hclose(hndl12);
    errno_assert(rc == 0);
    int hndl13 = go(worker2());
    errno_assert(hndl13 >= 0);
    rc = bundle_wait(hndl13, now() + 50);
    errno_assert(rc == -1 && errno == ETIMEDOUT);
    rc = hclose(hndl13);
    errno_assert(rc == 0);

    /* Test the deadline expiring in the same scheduler pass in which the last
       coroutine of the bundle finishes. */
    int64_t deadline = now() + 50;
    int hndl14 = go(worker4(deadline));
    errno_assert(hndl14 >= 0);
    rc = bundle_wait(hndl14, deadline);
    errno_assert(rc == -1 && errno == ETIMEDOUT);
    rc = hclose(hndl14);
    errno_assert(rc == 0);

    /* Test the waiter being canceled in the same scheduler pass in which
       the last coroutine of the bundle finishes. */
    int ch[2];
    rc = chmake(ch);
    errno_assert(rc == 0);
    int hndl15 = go(worker5(ch[0]));
    errno_assert(hndl15 >= 0);
    int hndl16 = go(waiter(hndl15));
    errno_assert(hndl16 >= 0);
    int val = 0;
    rc = chsend(ch[1], &val, sizeof(val), -1);
    errno_assert(rc == 0);
    rc = hclose(hndl16);
    errno_assert(rc == 0);
    rc = hclose(hndl15);
    errno_assert(rc == 0);
    rc = hclose(ch[0]);
    errno_assert(rc == 0);
    rc = hclose(ch[1]);
    errno_assert(rc == 0);

    test_close_while_waited_for();

    return 0;
}
