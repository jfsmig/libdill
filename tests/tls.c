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
#include <sys/socket.h>
#include <unistd.h>

#include "assert.h"
#include "../libdill.h"

#if defined DILL_THREADS

#include <pthread.h>

coroutine void handshake_client(int u) {
    int s = tls_attach_client(u, -1);
    errno_assert(s >= 0);
    u = tls_detach(s, -1);
    errno_assert(u >= 0);
    int rc = hclose(u);
    errno_assert(rc == 0);
}

static void *handshake_thread(void *arg) {
    int u[2];
    int rc = ipc_pair(u);
    errno_assert(rc == 0);
    int cr = go(handshake_client(u[1]));
    errno_assert(cr >= 0);
    int s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    u[0] = tls_detach(s, -1);
    errno_assert(u[0] >= 0);
    rc = hclose(u[0]);
    errno_assert(rc == 0);
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);
    return NULL;
}

/* The first attach in the process sets the TLS layer up. Several threads
   doing it at the same time must not set it up more than once. */
static void test_concurrent_first_attach(void) {
    pthread_t threads[8];
    int i;
    for(i = 0; i != 8; ++i) {
        int rc = pthread_create(&threads[i], NULL, handshake_thread, NULL);
        assert(rc == 0);
    }
    for(i = 0; i != 8; ++i) {
        int rc = pthread_join(threads[i], NULL);
        assert(rc == 0);
    }
}

#endif

coroutine void client1(int u) {
    int s = tls_attach_client(u, -1);
    errno_assert(s >= 0);
    int rc = bsend(s, "ABC", 3, -1);
    errno_assert(rc == 0);
    rc = tls_done(s, -1);
    errno_assert(rc == 0);
    char buf[3];
    rc = brecv(s, buf, sizeof(buf), -1);
    errno_assert(rc == 0);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    u = tls_detach(s, -1);
    errno_assert(u >= 0);
    rc = hclose(u);
    errno_assert(rc == 0);
}

static uint8_t busy_out[0x100000];
static uint8_t busy_in[sizeof(busy_out)];

coroutine void busy_sender(int s) {
    int rc = bsend(s, busy_out, sizeof(busy_out), -1);
    errno_assert(rc == 0);
}

coroutine void busy_receiver(int s) {
    char buf[3];
    int rc = brecv(s, buf, sizeof(buf), -1);
    errno_assert(rc == 0);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
}

coroutine void busy_peer(int u) {
    int s = tls_attach_server(u, "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    int rc = msleep(now() + 50);
    errno_assert(rc == 0);
    rc = brecv(s, busy_in, sizeof(busy_in), -1);
    errno_assert(rc == 0);
    assert(memcmp(busy_in, busy_out, sizeof(busy_in)) == 0);
    rc = bsend(s, "ABCDEF", 6, -1);
    errno_assert(rc == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

/* A coroutine that sends, or receives, on a socket where another one is
   blocked doing the same gets EBUSY and does no harm to the operation in
   progress. The send blocks because it exceeds what the transport buffers. */
static void test_busy(void) {
    size_t i;
    for(i = 0; i != sizeof(busy_out); ++i) busy_out[i] = (uint8_t)(i % 251);
    int u[2];
    int rc = ipc_pair(u);
    errno_assert(rc == 0);
    int peer = go(busy_peer(u[1]));
    errno_assert(peer >= 0);
    int s = tls_attach_client(u[0], -1);
    errno_assert(s >= 0);
    int sender = go(busy_sender(s));
    errno_assert(sender >= 0);
    int receiver = go(busy_receiver(s));
    errno_assert(receiver >= 0);
    char buf[3];
    rc = bsend(s, "X", 1, -1);
    errno_assert(rc == -1 && errno == EBUSY);
    rc = brecv(s, buf, sizeof(buf), -1);
    errno_assert(rc == -1 && errno == EBUSY);
    /* Nor can the socket be shut down or detached under them. */
    rc = tls_done(s, -1);
    errno_assert(rc == -1 && errno == EBUSY);
    rc = tls_detach(s, -1);
    errno_assert(rc == -1 && errno == EBUSY);
    rc = bundle_wait(sender, -1);
    errno_assert(rc == 0);
    rc = bundle_wait(receiver, -1);
    errno_assert(rc == 0);
    rc = brecv(s, buf, sizeof(buf), -1);
    errno_assert(rc == 0);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    rc = bundle_wait(peer, -1);
    errno_assert(rc == 0);
    rc = hclose(sender);
    errno_assert(rc == 0);
    rc = hclose(receiver);
    errno_assert(rc == 0);
    rc = hclose(peer);
    errno_assert(rc == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

coroutine void client2(int u) {
    int s = tls_attach_client(u, -1);
    errno_assert(s >= 0);
    int rc = bsend(s, "ABC", 3, -1);
    errno_assert(rc == 0);
    u = tls_detach(s, -1);
    errno_assert(u >= 0);
    rc = bsend(u, "DEF", 3, -1);
    errno_assert(rc == 0);
    rc = ipc_close(u, -1);
    errno_assert(rc == 0);
}

coroutine void silent_client(int u) {
    int s = tls_attach_client(u, -1);
    errno_assert(s >= 0);
    int rc = msleep(-1);
    errno_assert(rc == -1 && errno == ECANCELED);
    rc = hclose(s);
    errno_assert(rc == 0);
}

coroutine void late_detach_client(int u) {
    int s = tls_attach_client(u, -1);
    errno_assert(s >= 0);
    char buf[3];
    int rc = brecv(s, buf, sizeof(buf), -1);
    errno_assert(rc == 0);
    rc = msleep(now() + 100);
    errno_assert(rc == 0);
    u = tls_detach(s, -1);
    errno_assert(u >= 0);
    rc = hclose(u);
    errno_assert(rc == 0);
}

coroutine void client3(int u) {
    int s = tls_attach_client(u, -1);
    errno_assert(s >= 0);
    uint8_t c = 0;
    uint8_t b[2777];
    int i;
    for(i = 0; i != 257; ++i) {
        int j;
        for(j = 0; j != sizeof(b); j++) {
            b[j] = c;
            c++;
        }
        int rc = bsend(s, b, sizeof(b), -1);
        errno_assert(rc == 0);
    }
    u = tls_detach(s, -1);
    errno_assert(u >= 0);
    int rc = hclose(u);
    errno_assert(rc == 0);
}

coroutine void canceled_attach(int u, int server, int cancel_first) {
    if(cancel_first) {
        int rc = msleep(-1);
        errno_assert(rc == -1 && errno == ECANCELED);
    }
    int s = server ?
        tls_attach_server(u, "tests/cert.pem", "tests/key.pem", -1) :
        tls_attach_client(u, -1);
    errno_assert(s == -1 && errno == ECANCELED);
}

int main(void) {
    char buf[16];

#if defined DILL_THREADS
    test_concurrent_first_attach();
#endif
    test_busy();

    /* Test simple data exchange, with explicit handshake. */
    int u[2];
    int rc = ipc_pair(u);
    errno_assert(rc == 0);
    int cr = go(client1(u[1]));
    errno_assert(cr >= 0);
    struct tls_storage mem;
    int s = tls_attach_server_mem(u[0], "tests/cert.pem", "tests/key.pem",
        &mem, -1);
    errno_assert(s >= 0);
    rc = brecv(s, buf, 3, -1);
    errno_assert(rc == 0);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    rc = brecv(s, buf, 3, -1);
    errno_assert(rc == -1 && errno == EPIPE);
    rc = bsend(s, "DEF", 3, -1);
    errno_assert(rc == 0);
    rc = tls_done(s, -1);
    errno_assert(rc == 0);
    u[0] = tls_detach(s, -1);
    errno_assert(u[0] >= 0);
    /* The detached socket is gone, only the underlying one remains. */
    rc = hclose(s);
    errno_assert(rc == -1 && errno == EBADF);
    rc = hclose(u[0]);
    errno_assert(rc == 0);
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    /* tls_detach() gives up at its deadline if the peer doesn't answer. */
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    cr = go(silent_client(u[1]));
    errno_assert(cr >= 0);
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    rc = tls_done(s, -1);
    errno_assert(rc == 0);
    int64_t deadline = now() + 50;
    rc = tls_detach(s, deadline);
    errno_assert(rc == -1 && errno == ETIMEDOUT);
    time_assert(now() - deadline, 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    /* tls_detach() waits for the peer's answer until its own deadline,
       not until the expired deadline of an earlier operation. */
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    cr = go(late_detach_client(u[1]));
    errno_assert(cr >= 0);
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    rc = bsend(s, "ABC", 3, now() + 10);
    errno_assert(rc == 0);
    rc = msleep(now() + 30);
    errno_assert(rc == 0);
    u[0] = tls_detach(s, -1);
    errno_assert(u[0] >= 0);
    rc = hclose(u[0]);
    errno_assert(rc == 0);
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    /* tls_done() gives up at its deadline if the peer doesn't read. To make
       it block, the send buffer is filled up behind the socket's back, via
       a duplicate of the file descriptor, which ipc_fromfd() has switched
       to non-blocking mode. */
    int fds[2];
    rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    errno_assert(rc == 0);
    int fd = dup(fds[0]);
    errno_assert(fd >= 0);
    u[0] = ipc_fromfd(fds[0]);
    errno_assert(u[0] >= 0);
    u[1] = ipc_fromfd(fds[1]);
    errno_assert(u[1] >= 0);
    cr = go(silent_client(u[1]));
    errno_assert(cr >= 0);
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    while(send(fd, "", 1, 0) == 1) {}
    errno_assert(errno == EAGAIN || errno == EWOULDBLOCK);
    deadline = now() + 50;
    rc = tls_done(s, deadline);
    errno_assert(rc == -1 && errno == ETIMEDOUT);
    time_assert(now() - deadline, 0);
    rc = close(fd);
    errno_assert(rc == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    /* Test simple data transfer, terminated by tls_detach().
       Then send some data over undelying IPC connection. */
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    cr = go(client2(u[1]));
    errno_assert(cr >= 0);
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    rc = brecv(s, buf, 3, -1);
    errno_assert(rc == 0);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    u[0] = tls_detach(s, -1);
    errno_assert(u[0] >= 0);
    rc = brecv(u[0], buf, 3, -1);
    errno_assert(rc == 0);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    rc = ipc_close(u[0], -1);
    errno_assert(rc == 0);
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    /* Cancel the attach, either while it's blocked in the handshake with
       a silent peer or before it starts. It has to release everything. */
    int i;
    for(i = 0; i != 4; ++i) {
        rc = ipc_pair(u);
        errno_assert(rc == 0);
        cr = go(canceled_attach(u[1], i & 1, i & 2));
        errno_assert(cr >= 0);
        rc = hclose(cr);
        errno_assert(rc == 0);
        rc = hclose(u[0]);
        errno_assert(rc == 0);
    }

    /* Transfer large amount of data. */
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    cr = go(client3(u[1]));
    errno_assert(cr >= 0);
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    uint8_t c = 0;
    for(i = 0; i != 2777; ++i) {
        uint8_t b[257];
        rc = brecv(s, b, sizeof(b), -1);
        errno_assert(rc == 0);
        int j;
        for(j = 0; j != sizeof(b); ++j) {
            assert(b[j] == c);
            c++;
        }
    }
    u[0] = tls_detach(s, -1);
    errno_assert(u[0] >= 0);
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);
    rc = hclose(u[0]);
    errno_assert(rc == 0);

    /* Handshake with a peer that doesn't speak TLS fails. */
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    rc = bsend(u[1], "This is not a TLS record.", 25, -1);
    errno_assert(rc == 0);
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s == -1 && errno == EFAULT);
    rc = hclose(u[1]);
    errno_assert(rc == 0);
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    rc = bsend(u[1], "This is not a TLS record.", 25, -1);
    errno_assert(rc == 0);
    s = tls_attach_client(u[0], -1);
    errno_assert(s == -1 && errno == EFAULT);
    rc = hclose(u[1]);
    errno_assert(rc == 0);

    /* Handshake with a peer that closes the connection fails. */
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    rc = hclose(u[1]);
    errno_assert(rc == 0);
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s == -1 && errno == EPIPE);
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    rc = hclose(u[1]);
    errno_assert(rc == 0);
    s = tls_attach_client(u[0], -1);
    errno_assert(s == -1 && errno == ECONNRESET);

    /* Handshake with a silent peer fails when the deadline expires. */
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    int64_t start = now();
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", start + 50);
    errno_assert(s == -1 && errno == ETIMEDOUT);
    time_assert(now() - start, 50);
    rc = hclose(u[1]);
    errno_assert(rc == 0);
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    start = now();
    s = tls_attach_client(u[0], start + 50);
    errno_assert(s == -1 && errno == ETIMEDOUT);
    time_assert(now() - start, 50);
    rc = hclose(u[1]);
    errno_assert(rc == 0);

    return 0;
}

