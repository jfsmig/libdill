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
#include <sys/resource.h>

#include "assert.h"
#include "../libdill.h"

/* A client masks every frame with random bytes. Check that it fails cleanly,
   and recovers, when the source of randomness cannot be opened. This has to
   run before anything else sends a frame from the client side. */
static void client_send_without_random_source(void) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int s = ws_attach_client(p[1], WS_NOHTTP | WS_BINARY, NULL, NULL, -1);
    errno_assert(s >= 0);
    /* Leave no file descriptor available. */
    struct rlimit old;
    rc = getrlimit(RLIMIT_NOFILE, &old);
    errno_assert(rc == 0);
    struct rlimit none = old;
    none.rlim_cur = 0;
    rc = setrlimit(RLIMIT_NOFILE, &none);
    errno_assert(rc == 0);
    rc = msend(s, "ABC", 3, -1);
    errno_assert(rc == -1 && errno == EMFILE);
    rc = setrlimit(RLIMIT_NOFILE, &old);
    errno_assert(rc == 0);
    rc = msend(s, "ABC", 3, -1);
    errno_assert(rc == 0);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(p[0]);
    errno_assert(rc == 0);
}

coroutine void nohttp_client(int s) {
    s = ws_attach_client(s, WS_NOHTTP | WS_TEXT, NULL, NULL, -1);
    errno_assert(s >= 0);
    int rc = msend(s, "ABC", 3, -1);
    errno_assert(rc == 0);
    char buf[3];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 3);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz < 0 && errno == EPIPE);
    int status;
    sz = ws_status(s, &status, buf, sizeof(buf));
    errno_assert(sz >= 0);
    assert(status == 1000);
    assert(sz == 2);
    assert(buf[0] == 'O' && buf[1] == 'K');
    s = ws_detach(s, 0, NULL, 0, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

coroutine void http_client(int s) {
    s = ws_attach_client(s, WS_BINARY, "/", "www.example.org", -1);
    errno_assert(s >= 0);
    int rc = msend(s, "ABC", 3, -1);
    errno_assert(rc == 0);
    char buf[3];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 3);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz < 0 && errno == EPIPE);
    int status;
    sz = ws_status(s, &status, buf, sizeof(buf));
    errno_assert(sz >= 0);
    assert(status == 1000);
    assert(sz == 0);
    s = ws_detach(s, 0, NULL, 0, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

/* A handshake refused by the server closes the underlying socket. */
static void test_client_closes_socket_on_refusal(void) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    const char *reply = "HTTP/1.1 404 Not Found\r\n";
    rc = bsend(p[1], reply, strlen(reply), -1);
    errno_assert(rc == 0);
    int s = ws_attach_client(p[0], WS_BINARY, "/", "www.example.org", -1);
    assert(s == -1 && errno == EPROTO);
    /* The server reads the request, then the end of the stream. */
    do {
        char c;
        rc = brecv(p[1], &c, 1, now() + 100);
    } while(rc == 0);
    errno_assert(errno == EPIPE);
    rc = hclose(p[1]);
    errno_assert(rc == 0);
}

/* Reads what is left in the socket and checks that the peer then closed
   the connection, rather than keeping it open. */
static void expect_closed_by_peer(int s) {
    int rc;
    do {
        char c;
        rc = brecv(s, &c, 1, now() + 100);
    } while(rc == 0);
    errno_assert(errno == EPIPE);
    rc = hclose(s);
    errno_assert(rc == 0);
}

/* ws_detach() closes the socket when it fails. */
static void test_detach_closes_socket_on_error(void) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int s = ws_attach_server(p[0], WS_NOHTTP | WS_BINARY, NULL, 0, NULL, 0,
        -1);
    errno_assert(s >= 0);
    /* An unmasked frame, which a server must reject. */
    const uint8_t frame[] = {0x82, 0x00};
    rc = bsend(p[1], frame, sizeof(frame), -1);
    errno_assert(rc == 0);
    s = ws_detach(s, 1000, NULL, 0, -1);
    assert(s == -1 && errno == EPROTO);
    expect_closed_by_peer(p[1]);
}

/* ws_detach() fails if the peer ends the connection without sending
   a close frame. */
static void test_detach_needs_close_frame(void) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int s = ws_attach_server(p[0], WS_NOHTTP | WS_BINARY, NULL, 0, NULL, 0,
        -1);
    errno_assert(s >= 0);
    rc = ipc_done(p[1], -1);
    errno_assert(rc == 0);
    s = ws_detach(s, 1000, NULL, 0, -1);
    assert(s == -1 && errno == ECONNRESET);
    expect_closed_by_peer(p[1]);
}

/* Returns a WebSocket attached to one end of a socket pair. The frames
   are queued on the other end, returned in 'peer', as if sent by the peer. */
static int ws_fed_with(int server, const uint8_t *frames, size_t len,
      int *peer) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int s = server ?
        ws_attach_server(p[0], WS_NOHTTP | WS_BINARY, NULL, 0, NULL, 0, -1) :
        ws_attach_client(p[0], WS_NOHTTP | WS_BINARY, NULL, NULL, -1);
    errno_assert(s >= 0);
    rc = bsend(p[1], frames, len, -1);
    errno_assert(rc == 0);
    *peer = p[1];
    return s;
}

static void close_both(int s, int peer) {
    int rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(peer);
    errno_assert(rc == 0);
}

/* "ABCDEF" sent by a server as a message of two frames: "ABC" and "DEF". */
static const uint8_t fragmented_msg[] = {
    0x02, 0x03, 'A', 'B', 'C', 0x80, 0x03, 'D', 'E', 'F'};

/* Each frame of a message lands after the previous one. */
static void test_recv_fragmented_msg(void) {
    int peer;
    int s = ws_fed_with(0, fragmented_msg, sizeof(fragmented_msg), &peer);
    char buf[7] = {0};
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 6);
    assert(memcmp(buf, "ABCDEF", 6) == 0);
    close_both(s, peer);
}

/* The frames of a message cannot overflow the buffer between them. */
static void test_recv_fragmented_msg_too_long(void) {
    int peer;
    int s = ws_fed_with(0, fragmented_msg, sizeof(fragmented_msg), &peer);
    char buf[5];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == EMSGSIZE);
    close_both(s, peer);
}

/* A server unmasks a frame correctly when it spans several buffers. */
static void test_server_unmasks_across_buffers(void) {
    const uint8_t frames[] = {0x82, 0x86, 0x01, 0x02, 0x03, 0x04,
        'A' ^ 0x01, 'B' ^ 0x02, 'C' ^ 0x03, 'D' ^ 0x04, 'E' ^ 0x01, 'F' ^ 0x02};
    int peer;
    int s = ws_fed_with(1, frames, sizeof(frames), &peer);
    char buf1[3];
    char buf2[3];
    struct iolist iol2 = {buf2, sizeof(buf2), NULL, 0};
    struct iolist iol1 = {buf1, sizeof(buf1), &iol2, 0};
    ssize_t sz = mrecvl(s, &iol1, &iol2, -1);
    errno_assert(sz >= 0);
    assert(sz == 6);
    assert(memcmp(buf1, "ABC", 3) == 0);
    assert(memcmp(buf2, "DEF", 3) == 0);
    close_both(s, peer);
}

/* A server unmasks the reason of a close frame, which follows the status. */
static void test_server_unmasks_close_reason(void) {
    const uint8_t frames[] = {0x88, 0x84, 0x01, 0x02, 0x03, 0x04,
        0x03 ^ 0x01, 0xe8 ^ 0x02, 'O' ^ 0x03, 'K' ^ 0x04};
    int peer;
    int s = ws_fed_with(1, frames, sizeof(frames), &peer);
    char buf[3];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == EPIPE);
    int status;
    sz = ws_status(s, &status, buf, sizeof(buf));
    errno_assert(sz >= 0);
    assert(status == 1000);
    assert(sz == 2);
    assert(buf[0] == 'O' && buf[1] == 'K');
    close_both(s, peer);
}

/* A server skips, rather than unmasks, the payload it has no buffer for.
   ws_detach() itself drops pending messages that way. */
static void test_server_skips_masked_payload(void) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int s = ws_attach_server(p[0], WS_NOHTTP | WS_BINARY, NULL, 0, NULL, 0,
        -1);
    errno_assert(s >= 0);
    /* Masked frames, as sent by a client: two binary messages of 3 bytes
       and an empty close frame. */
    const uint8_t frames[] = {
        0x82, 0x83, 0x01, 0x02, 0x03, 0x04, 'A' ^ 0x01, 'B' ^ 0x02, 'C' ^ 0x03,
        0x82, 0x83, 0x01, 0x02, 0x03, 0x04, 'D' ^ 0x01, 'E' ^ 0x02, 'F' ^ 0x03,
        0x88, 0x80, 0x01, 0x02, 0x03, 0x04};
    rc = bsend(p[1], frames, sizeof(frames), -1);
    errno_assert(rc == 0);
    ssize_t sz = mrecv(s, NULL, 3, -1);
    errno_assert(sz >= 0);
    assert(sz == 3);
    s = ws_detach(s, 1000, NULL, 0, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(p[1]);
    errno_assert(rc == 0);
}

/* mrecvl() without any buffer drops the message and returns its size.
   The dropped message is longer than the buffer of the previous call, to
   catch a receive that reuses the leftovers of that call. */
static void test_recv_drops_msg_without_buffer(void) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int s = ws_attach_client(p[0], WS_NOHTTP | WS_BINARY, NULL, NULL, -1);
    errno_assert(s >= 0);
    /* Unmasked binary frames, as sent by a server: "XYZ", "ABCD" and "E". */
    const uint8_t frames[] = {0x82, 0x03, 'X', 'Y', 'Z',
        0x82, 0x04, 'A', 'B', 'C', 'D', 0x82, 0x01, 'E'};
    rc = bsend(p[1], frames, sizeof(frames), -1);
    errno_assert(rc == 0);
    char buf[3];
    struct iolist iol = {buf, sizeof(buf), NULL, 0};
    ssize_t sz = mrecvl(s, &iol, &iol, -1);
    errno_assert(sz >= 0);
    assert(sz == 3);
    sz = mrecvl(s, NULL, NULL, -1);
    errno_assert(sz >= 0);
    assert(sz == 4);
    assert(buf[0] == 'X' && buf[1] == 'Y' && buf[2] == 'Z');
    sz = mrecvl(s, &iol, &iol, -1);
    errno_assert(sz >= 0);
    assert(sz == 1);
    assert(buf[0] == 'E');
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(p[1]);
    errno_assert(rc == 0);
}

/* Returns a WebSocket attached to one end of a socket pair, with 'input'
   already written to the other end, which is returned in 'peer'. */
static int ws_with_input(int server, const uint8_t *input, size_t len,
      int *peer) {
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int s = server ?
        ws_attach_server(p[0], WS_NOHTTP | WS_BINARY, NULL, 0, NULL, 0, -1) :
        ws_attach_client(p[0], WS_NOHTTP | WS_BINARY, NULL, NULL, -1);
    errno_assert(s >= 0);
    rc = bsend(p[1], input, len, -1);
    errno_assert(rc == 0);
    *peer = p[1];
    return s;
}

static void close_ws_and_peer(int s, int peer) {
    int rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(peer);
    errno_assert(rc == 0);
}

/* After a protocol violation the rest of the stream is not parsed. Here
   the payload of the offending frame would pass for a frame of its own. */
static void test_recv_fails_after_protocol_error(void) {
    /* An unmasked frame, which a server must reject. Its payload is the
       masked frame of a 1-byte message. */
    const uint8_t input[] = {0x82, 0x07,
        0x82, 0x81, 0x01, 0x02, 0x03, 0x04, 'X' ^ 0x01};
    int peer;
    int s = ws_with_input(1, input, sizeof(input), &peer);
    char buf[16];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == EPROTO);
    sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == ECONNRESET);
    close_ws_and_peer(s, peer);
}

/* Same thing after a message that does not fit into the buffer. */
static void test_recv_fails_after_oversized_msg(void) {
    /* A message of 4 bytes. The last 3 are the frame of a 1-byte message. */
    const uint8_t input[] = {0x82, 0x04, 'A', 0x82, 0x01, 'X'};
    int peer;
    int s = ws_with_input(0, input, sizeof(input), &peer);
    char buf[1];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == EMSGSIZE);
    sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == ECONNRESET);
    close_ws_and_peer(s, peer);
}

/* A close frame that is invalid, or not received in full, does not count
   as the peer closing the connection. */
static void test_bad_close_frame_is_not_a_close(void) {
    const uint8_t bad_status[] = {0x88, 0x02, 999 >> 8, 999 & 0xff};
    int peer;
    int s = ws_with_input(0, bad_status, sizeof(bad_status), &peer);
    char buf[16];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == EPROTO);
    sz = ws_status(s, NULL, NULL, 0);
    assert(sz == -1 && errno == EAGAIN);
    sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == ECONNRESET);
    close_ws_and_peer(s, peer);

    const uint8_t truncated[] = {0x88, 0x02, 1000 >> 8};
    s = ws_with_input(0, truncated, sizeof(truncated), &peer);
    sz = mrecv(s, buf, sizeof(buf), now() + 50);
    assert(sz == -1 && errno == ETIMEDOUT);
    sz = ws_status(s, NULL, NULL, 0);
    assert(sz == -1 && errno == EAGAIN);
    close_ws_and_peer(s, peer);
}

coroutine void first_receiver(int s) {
    char buf[16];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz == 3);
    assert(memcmp(buf, "one", 3) == 0);
}

/* A receive refused because another coroutine is already receiving has
   read nothing from the stream, so it is not a receive error. */
static void test_recv_works_after_busy(void) {
    /* The first receiver blocks in the middle of a frame header. */
    const uint8_t start[] = {0x82};
    const uint8_t rest[] = {0x03, 'o', 'n', 'e', 0x82, 0x03, 't', 'w', 'o'};
    int peer;
    int s = ws_with_input(0, start, sizeof(start), &peer);
    int cr = go(first_receiver(s));
    errno_assert(cr >= 0);
    char buf[16];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == EBUSY);
    int rc = bsend(peer, rest, sizeof(rest), -1);
    errno_assert(rc == 0);
    rc = bundle_wait(cr, -1);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);
    sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz == 3);
    assert(memcmp(buf, "two", 3) == 0);
    close_ws_and_peer(s, peer);
}

/* Longer than 65535 bytes, so that the frame header carries a 64-bit
   payload length. */
static uint8_t large_msg[65536];
static uint8_t large_buf[sizeof(large_msg)];

coroutine void large_msg_client(int s) {
    s = ws_attach_client(s, WS_NOHTTP | WS_BINARY, NULL, NULL, -1);
    errno_assert(s >= 0);
    int rc = msend(s, large_msg, sizeof(large_msg), -1);
    errno_assert(rc == 0);
    s = ws_detach(s, 1000, NULL, 0, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(rc == 0);
}

static void test_client_sends_large_msg(void) {
    size_t i;
    for(i = 0; i != sizeof(large_msg); ++i) large_msg[i] = (uint8_t)(i % 251);
    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int cr = go(large_msg_client(p[1]));
    errno_assert(cr >= 0);
    int s = ws_attach_server(p[0], WS_NOHTTP | WS_BINARY, NULL, 0, NULL, 0,
        -1);
    errno_assert(s >= 0);
    ssize_t sz = mrecv(s, large_buf, sizeof(large_buf), -1);
    errno_assert(sz >= 0);
    assert(sz == sizeof(large_msg));
    assert(memcmp(large_buf, large_msg, sizeof(large_msg)) == 0);
    s = ws_detach(s, 1000, NULL, 0, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);
}

int main(void) {
    client_send_without_random_source();

    int p[2];
    int rc = ipc_pair(p);
    errno_assert(rc == 0);
    int cr = go(nohttp_client(p[1]));
    errno_assert(cr >= 0);
    int s = ws_attach_server(p[0], WS_NOHTTP | WS_TEXT, NULL, 0, NULL, 0, -1);
    errno_assert(s >= 0);
    char buf[3];
    ssize_t sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 3);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    rc = msend(s, "DEF", 3, -1);
    errno_assert(rc == 0);
    s = ws_detach(s, 1000, "OK", 2, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    /* The request key is a base64-encoded 16-byte nonce. It comes from the
       peer, so it may hold bytes that are not ASCII; none of them is a base64
       character. */
    char response_key[WS_KEY_SIZE];
    rc = ws_response_key("dGhlIHNhbXBsZSBub25jZQ==", response_key);
    errno_assert(rc == 0);
    assert(strcmp(response_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
    int byte;
    for(byte = 0x80; byte != 0x100; ++byte) {
        char request_key[] = "dGhlIHNhbXBsZSBub25jZQ==";
        request_key[0] = (char)byte;
        rc = ws_response_key(request_key, response_key);
        assert(rc == -1 && errno == EPROTO);
    }

    rc = ipc_pair(p);
    errno_assert(rc == 0);
    cr = go(http_client(p[1]));
    errno_assert(cr >= 0);
    char resource[256];
    char host[256];
    s = ws_attach_server(p[0], WS_BINARY, resource, sizeof(resource),
        host, sizeof(host), -1);
    errno_assert(s >= 0);
    assert(strcmp(resource, "/") == 0);
    assert(strcmp(host, "www.example.org") == 0);
    sz = mrecv(s, buf, sizeof(buf), -1);
    errno_assert(sz >= 0);
    assert(sz == 3);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    rc = msend(s, "DEF", 3, -1);
    errno_assert(rc == 0);
    s = ws_detach(s, 1000, NULL, 0, -1);
    errno_assert(s >= 0);
    rc = hclose(s);
    errno_assert(rc == 0);
    rc = hclose(cr);
    errno_assert(rc == 0);

    test_client_closes_socket_on_refusal();
    test_detach_closes_socket_on_error();
    test_detach_needs_close_frame();
    test_recv_fragmented_msg();
    test_recv_fragmented_msg_too_long();
    test_server_unmasks_across_buffers();
    test_server_unmasks_close_reason();
    test_server_skips_masked_payload();
    test_recv_drops_msg_without_buffer();
    test_recv_fails_after_protocol_error();
    test_recv_fails_after_oversized_msg();
    test_bad_close_frame_is_not_a_close();
    test_recv_works_after_busy();
    test_client_sends_large_msg();
    
    /* The detached socket is gone, only the underlying one remains. */
    rc = ipc_pair(p);
    errno_assert(rc == 0);
    struct ws_storage mem;
    int s0 = ws_attach_server_mem(p[0], WS_NOHTTP | WS_TEXT, NULL, 0, NULL, 0,
        &mem, -1);
    errno_assert(s0 >= 0);
    int s1 = ws_attach_client(p[1], WS_NOHTTP | WS_TEXT, NULL, NULL, -1);
    errno_assert(s1 >= 0);
    rc = ws_done(s1, 1000, NULL, 0, -1);
    errno_assert(rc == 0);
    p[0] = ws_detach(s0, 1000, NULL, 0, -1);
    errno_assert(p[0] >= 0);
    rc = hclose(s0);
    errno_assert(rc == -1 && errno == EBADF);
    p[1] = ws_detach(s1, 0, NULL, 0, -1);
    errno_assert(p[1] >= 0);
    rc = hclose(p[1]);
    errno_assert(rc == 0);
    rc = hclose(p[0]);
    errno_assert(rc == 0);

    return 0;
}

