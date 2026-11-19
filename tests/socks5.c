/*

  Copyright (c) 2018 Joseph deBlaquiere

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
#include "../libdill.h"
#include <stdio.h>
#include <string.h>

static char *auth_user = NULL;
static char *auth_pass = NULL;

int auth_fn(const char *user, const char* pass) {
    if(auth_user) {
        if(!user) return 0;
        if(strcmp(user, auth_user) != 0) return 0;
    }
    if(auth_pass) {
        if(!pass) return 0;
        if(strcmp(pass, auth_pass) != 0) return 0;
    }
    return 1;
}

void client_sayhello(int s) {
    uint8_t buf[16];
    strcpy((char *)buf, "hello");
    int err = bsend(s, buf, 5, now() + 5000);
    assert(!err);
    err = brecv(s, buf, 5, now() + 5000);
    assert(!err);
    buf[5] = '\x00';
    assert(strcmp((char *)buf,"olleh") == 0);
    return;
}

// An empty host name can't be put into a CONNECT request. The client must
// refuse it and send nothing past the method negotiation.
static void client_rejects_empty_hostname(void) {
    int h[2];
    int rc = ipc_pair(h);
    errno_assert(rc == 0);
    // let the client pass the method negotiation
    uint8_t selection[] = {0x05, 0x00};
    rc = bsend(h[0], selection, sizeof(selection), -1);
    errno_assert(rc == 0);
    rc = socks5_client_connectbyname(h[1], NULL, NULL, "", 5555, now() + 100);
    errno_assert(rc == -1 && errno == EINVAL);
    rc = hclose(h[1]);
    errno_assert(rc == 0);
    uint8_t methods[3];
    rc = brecv(h[0], methods, sizeof(methods), now() + 1000);
    errno_assert(rc == 0);
    uint8_t request;
    rc = brecv(h[0], &request, 1, now() + 1000);
    errno_assert(rc == -1 && errno == EPIPE);
    rc = hclose(h[0]);
    errno_assert(rc == 0);
}

coroutine void clientbyaddr(int s, char *user, char* pass) {
    struct ipaddr addr;
    int rc = ipaddr_remote(&addr, "127.0.0.1", 5555, 0, -1);
    assert(rc == 0);
    int err = socks5_client_connect(s, user, pass, &addr,
        now() + 5000);
    if(err) perror("Error connecting to addr=127.0.0.1:5555");
    assert(err == 0);
    client_sayhello(s);
    tcp_close(s, now() + 5000);
    return;
}

coroutine void clientbyipstring(int s, char *user, char* pass) {
    int err = socks5_client_connectbyname(s, user, pass, "127.0.0.1", 5555,
        now() + 5000);
    if(err) perror("Error connecting to \"127.0.0.1:5555\"");
    assert(err == 0);
    client_sayhello(s);
    tcp_close(s, now() + 5000);
    return;
}

coroutine void clientbyname(int s, char *user, char* pass) {
    int err = socks5_client_connectbyname(s, user, pass, "localhost", 5555,
        now() + 5000);
    if(err) perror("Error connecting to \"localhost:80\"");
    assert(err == 0);
    client_sayhello(s);
    tcp_close(s, now() + 5000);
    return;
}

void server(int s) {
    uint8_t buf[16];
    // impersonate server
    int err = brecv(s, buf, 5, now() + 5000);
    assert(!err);
    buf[5] = '\x00';
    assert(strcmp((char *)buf,"hello") == 0);
    strcpy((char *)buf, "olleh");
    err = bsend(s, buf, 5, now() + 5000);
    assert(!err);
    return;
}

coroutine void proxy_byaddr(int s, char *user, char* pass) {
    // set up auth
    if(auth_user) {free(auth_user); auth_user = NULL;}
    if(auth_pass) {free(auth_pass); auth_pass = NULL;}
    if(user) {
        auth_user = malloc(strlen(user)+1); strcpy(auth_user, user);
    } else {
        auth_user = NULL;
    }
    if(pass) {
        auth_pass = malloc(strlen(pass)+1); strcpy(auth_pass, pass);
    } else {
        auth_pass = NULL;
    }

    int err;
    if((!user) || (!pass)){
        err = socks5_proxy_auth(s, NULL, now() + 1000);
    } else {
        err = socks5_proxy_auth(s, auth_fn, now() + 1000);
    }
    assert(err == 0);

    struct ipaddr addr;
    int cmd = socks5_proxy_recvcommand(s, &addr, now() + 1000);
    assert(cmd > 0);
    assert(cmd == SOCKS5_CONNECT);

    err = ipaddr_remote(&addr, "0.0.0.0", 0, IPADDR_IPV4, -1);
    assert(err == 0);

    err = socks5_proxy_sendreply(s, SOCKS5_SUCCESS, &addr, now() + 1000);
    assert(err == 0);

    server(s);
    return;
}

coroutine void proxy_byname(int s, char *user, char* pass) {
    // set up auth
    if(auth_user) {free(auth_user); auth_user = NULL;}
    if(auth_pass) {free(auth_pass); auth_pass = NULL;}
    if(user) {
        auth_user = malloc(strlen(user)+1); strcpy(auth_user, user);
    } else {
        auth_user = NULL;
    }
    if(pass) {
        auth_pass = malloc(strlen(pass)+1); strcpy(auth_pass, pass);
    } else {
        auth_pass = NULL;
    }

    int err;
    if((!user) || (!pass)){
        err = socks5_proxy_auth(s, NULL, now() + 1000);
    } else {
        err = socks5_proxy_auth(s, auth_fn, now() + 1000);
    }
    assert(err == 0);

    struct ipaddr addr;
    char r_name[256];
    int r_port;
    int cmd = socks5_proxy_recvcommandbyname(s, r_name, &r_port, now() + 1000);
    assert(cmd > 0);
    assert(cmd == SOCKS5_CONNECT);
    assert((strcmp(r_name, "127.0.0.1") == 0) ||
           (strcmp(r_name, "localhost") == 0));
    assert(r_port == 5555);

    err = ipaddr_remote(&addr, "0.0.0.0", 0, IPADDR_IPV4, -1);
    assert(err == 0);

    err = socks5_proxy_sendreply(s, SOCKS5_SUCCESS, &addr, now() + 1000);
    assert(err == 0);

    server(s);
    return;
}

// The proxy answers the method negotiation with a method that the client,
// having no credentials, did not offer. The client must report a protocol
// error instead of trusting the proxy.
static void client_rejects_unoffered_method(uint8_t method) {
    int h[2];
    int rc = ipc_pair(h);
    errno_assert(rc == 0);
    uint8_t selection[2] = {0x05, method};
    rc = bsend(h[0], selection, sizeof(selection), -1);
    errno_assert(rc == 0);
    struct ipaddr addr;
    rc = ipaddr_local(&addr, "127.0.0.1", 5555, 0);
    errno_assert(rc == 0);
    rc = socks5_client_connect(h[1], NULL, NULL, &addr, now() + 1000);
    errno_assert(rc == -1 && errno == EPROTO);
    rc = hclose(h[1]);
    errno_assert(rc == 0);
    rc = hclose(h[0]);
    errno_assert(rc == 0);
}

typedef char *up_t[2];
typedef void test_fn(int s, char *user, char* pass);

int main(void) {
    int h[2];
    int rc = ipc_pair(h);
    assert(rc == 0);
    up_t up[] = {{NULL, NULL}, {"user", "pass"}};
    test_fn *ctest[] = {clientbyaddr, clientbyipstring};
    test_fn *ptest[] = {proxy_byaddr, proxy_byname};

    for (int pi = 0; pi < 2; pi++) {
        for (int ci = 0; ci < 2; ci++) {
            for (int ui = 0; ui < 2; ui++) {
                printf("testing up[%d], ctest[%d], ptest[%d]\n", ui, ci, pi);
                int b = bundle();
                assert(b >= 0);
                rc = bundle_go(b, (ptest[pi])(h[0], up[ui][0], up[ui][1]));
                assert(rc == 0);
                rc = bundle_go(b, (ctest[ci])(h[1], up[ui][0], up[ui][1]));
                assert(rc == 0);
                rc = bundle_wait(b, -1);
                assert(rc == 0);
            }
        }
    }

    // username/password, then GSSAPI, which libdill never offers
    client_rejects_unoffered_method(0x02);
    client_rejects_unoffered_method(0x01);
    client_rejects_empty_hostname();

    return 0;
}
