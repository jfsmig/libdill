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

coroutine void dummy(void) {
    int rc = msleep(now() + 50);
    errno_assert(rc == 0);
}

int main() {
    int i;
    for(i = 0; i != 5; ++i) {
        /* Test go_mem. */
        char *stack = malloc(65536);
        assert(stack);
        int cr = go_mem(dummy(), stack, 65536);
        errno_assert(cr >= 0);
        int rc = msleep(now() + 100);
        errno_assert(rc == 0);
        rc = hclose(cr);
        errno_assert(rc == 0);
        free(stack);
    }

    /* Test go_mem with memory too small to hold the coroutine. The memory
       is taken from the middle of a larger block so that nothing else gets
       overwritten if the coroutine is launched anyway. */
    char *block = malloc(65536);
    assert(block);
    char *middle = block + 32768;
    int cr = go_mem(dummy(), middle, 32);
    errno_assert(cr == -1 && errno == ENOMEM);
    /* Here nothing is left once the top of the stack is aligned. */
    int b = bundle();
    errno_assert(b >= 0);
    int rc = bundle_go_mem(b, dummy(), middle + 8, 4);
    errno_assert(rc == -1 && errno == ENOMEM);
    rc = hclose(b);
    errno_assert(rc == 0);
    free(block);

    return 0;
}

