/*

  Copyright (c) 2016 Martin Sustrik

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

#include "assert.h"
#include "../libdillimpl.h"

static int status = 0;

struct test {
    struct hvfs vfs;
};

static void *test_query(struct hvfs *vfs, const void *type) {
    status = 1;
    return &status;
}

static void test_close(struct hvfs *vfs) {
    status = 2;
}

/* More handles than the handle table initially has room for. */
#define MANY_HANDLES 256

coroutine void canceled(void) {
    int rc = msleep(-1);
    errno_assert(rc == -1 && errno == ECANCELED);
}

coroutine void grow_handle_table(struct hvfs *vfs) {
    int rc = yield();
    errno_assert(rc == 0);
    int hndls[MANY_HANDLES];
    int i;
    for(i = 0; i != MANY_HANDLES; ++i) {
        hndls[i] = hmake(vfs);
        errno_assert(hndls[i] >= 0);
    }
    for(i = 0; i != MANY_HANDLES; ++i) {
        rc = hclose(hndls[i]);
        errno_assert(rc == 0);
    }
}

coroutine void close_again(int h) {
    /* Let the main coroutine start closing the handle. */
    int rc = yield();
    errno_assert(rc == 0);
    rc = hclose(h);
    errno_assert(rc == -1 && errno == EBADF);
}

int main(void) {

    struct test t;
    t.vfs.query = test_query;
    t.vfs.close = test_close;
    int h = hmake(&t.vfs);
    errno_assert(h >= 0);
    void *p = hquery(h, NULL);
    errno_assert(p);
    assert(p == &status);
    assert(status == 1);
    int rc = hclose(h);
    errno_assert(rc == 0);
    assert(status == 2);

    /* Test closing a handle while another coroutine makes the handle table
       grow: hclose() of a bundle lets grow_handle_table() run. The table is
       still the last block on the heap here; allocating right after it keeps
       realloc() from growing it in place. */
    void *pin = malloc(1);
    assert(pin);
    struct bundle_storage mem;
    int b = bundle_mem(&mem);
    errno_assert(b >= 0);
    rc = bundle_go(b, canceled());
    errno_assert(rc == 0);
    int grower = go(grow_handle_table(&t.vfs));
    errno_assert(grower >= 0);
    rc = hclose(b);
    errno_assert(rc == 0);
    p = hquery(b, NULL);
    errno_assert(p == NULL && errno == EBADF);
    rc = hclose(grower);
    errno_assert(rc == 0);
    free(pin);

    int ch[2];
    rc = chmake(ch);
    errno_assert(rc == 0);
    ch[0] = hown(ch[0]);
    errno_assert(ch[0] >= 0);
    ch[1] = hown(ch[1]);
    errno_assert(ch[1] >= 0);
    rc = hclose(ch[0]);
    errno_assert(rc == 0);
    rc = hclose(ch[1]);
    errno_assert(rc == 0);

    /* Test that handle numbers rotate: whatever the number of open handles,
       even after the handle table has grown, the number of a handle that was
       just closed is not handed out again straight away. */
    int hndls[600];
    int i;
    for(i = 0; i != 600; ++i) {
        hndls[i] = hmake(&t.vfs);
        errno_assert(hndls[i] >= 0);
        int closed = hmake(&t.vfs);
        errno_assert(closed >= 0);
        rc = hclose(closed);
        errno_assert(rc == 0);
        int next = hmake(&t.vfs);
        errno_assert(next >= 0);
        assert(next != closed);
        rc = hclose(next);
        errno_assert(rc == 0);
    }
    for(i = 0; i != 600; ++i) {
        rc = hclose(hndls[i]);
        errno_assert(rc == 0);
    }

    /* Closing a bundle lets other coroutines run. To them the handle is
       already gone: they cannot close it once more. */
    int bndl = bundle();
    errno_assert(bndl >= 0);
    rc = bundle_go(bndl, canceled());
    errno_assert(rc == 0);
    int closer = go(close_again(bndl));
    errno_assert(closer >= 0);
    rc = hclose(bndl);
    errno_assert(rc == 0);
    rc = hclose(closer);
    errno_assert(rc == 0);

    return 0;
}

