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

static int relay_delay = 0;

/* Forwards TLS records one by one. While relay_delay is set, the body of
   a record follows its header only after that many milliseconds. */
coroutine void relay(int from, int to) {
    while(1) {
        uint8_t hdr[5];
        uint8_t body[0x10000];
        int rc = brecv(from, hdr, sizeof(hdr), -1);
        if(rc < 0) return;
        size_t len = (hdr[3] << 8) | hdr[4];
        rc = brecv(from, body, len, -1);
        if(rc < 0) return;
        rc = bsend(to, hdr, sizeof(hdr), -1);
        if(rc < 0) return;
        if(relay_delay) {
            rc = msleep(now() + relay_delay);
            if(rc < 0) return;
        }
        rc = bsend(to, body, len, -1);
        if(rc < 0) return;
    }
}

coroutine void duplex_server(int u) {
    int s = tls_attach_server(u, "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    char buf[3];
    int rc = brecv(s, buf, sizeof(buf), -1);
    errno_assert(rc == 0);
    rc = bsend(s, "DEF", 3, -1);
    errno_assert(rc == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

coroutine void duplex_reader(int s) {
    char buf[3];
    int rc = brecv(s, buf, sizeof(buf), -1);
    errno_assert(rc == 0);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
}

/* One coroutine receives with no deadline while another one sends with
   a short deadline. The answer reaches the receiver in two parts, the
   second one after the sender's deadline has expired. */
static void test_receiver_keeps_deadline(void) {
    int client[2];
    int rc = ipc_pair(client);
    errno_assert(rc == 0);
    int server[2];
    rc = ipc_pair(server);
    errno_assert(rc == 0);
    int relays = bundle();
    errno_assert(relays >= 0);
    rc = bundle_go(relays, relay(client[1], server[0]));
    errno_assert(rc == 0);
    rc = bundle_go(relays, relay(server[0], client[1]));
    errno_assert(rc == 0);
    int peer = go(duplex_server(server[1]));
    errno_assert(peer >= 0);
    int s = tls_attach_client(client[0], -1);
    errno_assert(s >= 0);
    relay_delay = 50;
    int reader = go(duplex_reader(s));
    errno_assert(reader >= 0);
    rc = bsend(s, "ABC", 3, now() + 10);
    errno_assert(rc == 0);
    rc = bundle_wait(reader, -1);
    errno_assert(rc == 0);
    rc = bundle_wait(peer, -1);
    errno_assert(rc == 0);
    relay_delay = 0;
    rc = hclose(reader);
    errno_assert(rc == 0);
    rc = hclose(peer);
    errno_assert(rc == 0);
    rc = hclose(relays);
    errno_assert(rc == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(client[1]);
    errno_assert(rc == 0);
    rc = hclose(server[0]);
    errno_assert(rc == 0);
}

static uint8_t bulk_out[0x100000];
static uint8_t bulk_in[sizeof(bulk_out)];

coroutine void bulk_sender(int s) {
    int rc = bsend(s, bulk_out, sizeof(bulk_out), -1);
    errno_assert(rc == 0);
}

coroutine void late_bulk_receiver(int u) {
    int s = tls_attach_server(u, "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    int rc = msleep(now() + 50);
    errno_assert(rc == 0);
    rc = brecv(s, bulk_in, sizeof(bulk_in), -1);
    errno_assert(rc == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

/* One coroutine sends, with no deadline, more than the transport can
   buffer. While it's blocked, a receive on the same socket times out.
   The sender has to block again once the peer starts reading. */
static void test_sender_keeps_deadline(void) {
    int u[2];
    int rc = ipc_pair(u);
    errno_assert(rc == 0);
    int peer = go(late_bulk_receiver(u[1]));
    errno_assert(peer >= 0);
    int s = tls_attach_client(u[0], -1);
    errno_assert(s >= 0);
    int sender = go(bulk_sender(s));
    errno_assert(sender >= 0);
    char c;
    rc = brecv(s, &c, 1, now() + 10);
    errno_assert(rc == -1 && errno == ETIMEDOUT);
    rc = bundle_wait(sender, -1);
    errno_assert(rc == 0);
    rc = bundle_wait(peer, -1);
    errno_assert(rc == 0);
    rc = hclose(sender);
    errno_assert(rc == 0);
    rc = hclose(peer);
    errno_assert(rc == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

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

int main(void) {
    char buf[16];

    test_receiver_keeps_deadline();
    test_sender_keeps_deadline();

    /* Test simple data exchange, with explicit handshake. */
    int u[2];
    int rc = ipc_pair(u);
    errno_assert(rc == 0);
    int cr = go(client1(u[1]));
    errno_assert(cr >= 0);
    int s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
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
    rc = hclose(u[0]);
    errno_assert(rc == 0);
    rc = bundle_wait(cr, -1);
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

    /* Transfer large amount of data. */
    rc = ipc_pair(u);
    errno_assert(rc == 0);
    cr = go(client3(u[1]));
    errno_assert(cr >= 0);
    s = tls_attach_server(u[0], "tests/cert.pem", "tests/key.pem", -1);
    errno_assert(s >= 0);
    uint8_t c = 0;
    int i;
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
    
    return 0;
}

