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

#include <stdio.h>
#include <stdlib.h>

#include "assert.h"
#include "../libdill.h"

#define ITERATIONS 1000

coroutine void client(int s) {
    char bufs[5][256];
    struct iolist iol[5];
    int i;
    struct iolist *it;
    int counter = 0;
    int j;
    for(j = 0; j != ITERATIONS; ++j) {
        for(i = 0; i != 5; ++i) {
           iol[i].iol_base = bufs[i];
           iol[i].iol_len = random() % 32;
           iol[i].iol_next = &iol[i + 1];
           iol[i].iol_rsvd = 0;
        }
        iol[4].iol_next = NULL;
        for(it = &iol[0]; it; it = it->iol_next) {
            for(i = 0; i != it->iol_len; ++i) {
                ((char*)it->iol_base)[i] = (char)(counter++ % 128);
            }
        }
        int rc = bsendl(s, &iol[0], &iol[4], -1);
        errno_assert(rc == 0);
    }
    int rc = ipc_close(s, -1);
    errno_assert(rc == 0);
}

/* Checks that a malformed iolist is rejected. Each attempt gets a socket
   of its own, because a failed send leaves the socket unusable. */
static void send_malformed(struct iolist *first, struct iolist *last) {
    int ss[2];
    int rc = ipc_pair(ss);
    errno_assert(rc == 0);
    rc = bsendl(ss[0], first, last, -1);
    errno_assert(rc == -1 && errno == EINVAL);
    rc = hclose(ss[0]);
    errno_assert(rc == 0);
    rc = hclose(ss[1]);
    errno_assert(rc == 0);
}

int main() {
    int ss[2];
    int rc = ipc_pair(ss);
    errno_assert(rc == 0);
    int cr = go(client(ss[1]));
    errno_assert(cr >= 0);
    char bufs[5][256];
    struct iolist iol[5];
    int i;
    struct iolist *it;
    int counter = 0;
    while(1) {
        for(i = 0; i != 5; ++i) {
           if(random() % 10 == 0)
               iol[i].iol_base = NULL;
           else
               iol[i].iol_base = bufs[i];
           iol[i].iol_len = random() % 32;
           iol[i].iol_next = &iol[i + 1];
           iol[i].iol_rsvd = 0;
        }
        iol[4].iol_next = NULL;
        rc = brecvl(ss[0], &iol[0], &iol[4], -1);
        if(rc < 0 && errno == EPIPE) break;
        errno_assert(rc == 0);
        for(it = &iol[0]; it; it = it->iol_next) {
            if(!it->iol_base) {counter += it->iol_len; continue;}
            for(i = 0; i != it->iol_len; ++i) {
                char c = ((char*)it->iol_base)[i];
                assert(c == (char)(counter % 128));
                counter++;
            }
        }
    }
    rc = ipc_close(ss[0], -1);
    errno_assert(rc == 0);
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    /* A rejected iolist is handed back unchanged and can be used again. */
    struct iolist stray = {(void*)"D", 1, NULL, 0};
    struct iolist tail = {(void*)"C", 1, NULL, 0};
    struct iolist mid = {(void*)"B", 1, &tail, 0};
    struct iolist head = {(void*)"A", 1, &mid, 0};
    /* The last item is not a part of the list. */
    send_malformed(&head, &stray);
    assert(!head.iol_rsvd && !mid.iol_rsvd && !tail.iol_rsvd);
    /* There is a loop in the list. */
    tail.iol_next = &mid;
    send_malformed(&head, &stray);
    assert(!head.iol_rsvd && !mid.iol_rsvd && !tail.iol_rsvd);
    tail.iol_next = NULL;
    /* A reserved field is set. */
    mid.iol_rsvd = 1;
    send_malformed(&head, &tail);
    assert(!head.iol_rsvd && mid.iol_rsvd == 1 && !tail.iol_rsvd);
    mid.iol_rsvd = 0;
    rc = ipc_pair(ss);
    errno_assert(rc == 0);
    rc = bsendl(ss[0], &head, &tail, -1);
    errno_assert(rc == 0);
    rc = brecv(ss[1], bufs[0], 3, -1);
    errno_assert(rc == 0);
    assert(bufs[0][0] == 'A' && bufs[0][1] == 'B' && bufs[0][2] == 'C');
    rc = hclose(ss[0]);
    errno_assert(rc == 0);
    rc = hclose(ss[1]);
    errno_assert(rc == 0);
    return 0;
}

