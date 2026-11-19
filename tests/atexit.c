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

#include <stdlib.h>

#include "assert.h"
#include "../libdill.h"

/* Runs after libdill has cleaned up after itself: handlers registered with
   atexit() are called in the reverse order of their registration. */
static void use_libdill_at_exit(void) {
    int rc = msleep(now() + 1);
    errno_assert(rc == 0);
}

int main(void) {
    /* This has to precede the first use of libdill. */
    int rc = atexit(use_libdill_at_exit);
    assert(rc == 0);
    rc = msleep(now() + 1);
    errno_assert(rc == 0);
    return 0;
}

