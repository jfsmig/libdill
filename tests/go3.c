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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "assert.h"
#include "../libdill.h"

static int worker_done = 0;

coroutine void worker(void) {
    int rc = msleep(now() + 1000);
    assert(rc == -1 && errno == ECANCELED);
    /* Try again to test whether subsequent calls fail as well. */
    rc = msleep(now() + 1000);
    assert(rc == -1 && errno == ECANCELED);
    ++worker_done;
}

static int nested_done = 0;

coroutine void leaf(void) {
    int rc = msleep(-1);
    assert(rc == -1 && errno == ECANCELED);
    ++nested_done;
}

coroutine void mid(void) {
    int h = go(leaf());
    errno_assert(h >= 0);
    int rc = hclose(h);
    errno_assert(rc == 0);
    ++nested_done;
}

int main() {
    /* Test whether immediate cancelation works. */
    int cr1 = go(worker());
    errno_assert(cr1 >= 0);
    int cr2 = go(worker());
    errno_assert(cr2 >= 0);
    int cr3 = go(worker());
    errno_assert(cr3 >= 0);
    int rc = msleep(now() + 30);
    errno_assert(rc == 0);
    rc = hclose(cr1);
    errno_assert(rc == 0);
    rc = hclose(cr2);
    errno_assert(rc == 0);
    rc = hclose(cr3);
    errno_assert(rc == 0);
    assert(worker_done == 3);

    /* Test closing a coroutine that is itself waiting inside hclose()
       of its child. */
    int cr4 = go(mid());
    errno_assert(cr4 >= 0);
    rc = yield();
    errno_assert(rc == 0);
    rc = hclose(cr4);
    errno_assert(rc == 0);
    assert(nested_done == 2);

    return 0;
}

