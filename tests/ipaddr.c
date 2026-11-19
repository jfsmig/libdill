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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <string.h>

#include "assert.h"
#include "../libdill.h"

int main(void) {

    /* Test conversion of string to IPv4 address and back again. */
    struct ipaddr addr;
    int rc = ipaddr_local(&addr, "1.2.3.4", 5555, 0);
    errno_assert(rc == 0);
    int family = ipaddr_family(&addr);
    assert(family == AF_INET);
    const struct sockaddr_in *sin =
        (const struct sockaddr_in*)ipaddr_sockaddr(&addr);
    uint32_t a = *(uint32_t*)(&sin->sin_addr);
    assert(ntohl(a) == 0x01020304);
    assert(ntohs(sin->sin_port) == 5555);
    int port = ipaddr_port(&addr);
    assert(port == 5555);
    char buf[IPADDR_MAXSTRLEN];
    ipaddr_str(&addr, buf);
    assert(strcmp(buf, "1.2.3.4") == 0);

    /* Test conversion of string to IPv6 address and back again. */
    rc = ipaddr_local(&addr, "::1", 5555, 0);
    errno_assert(rc == 0);
    family = ipaddr_family(&addr);
    assert(family == AF_INET6);
    const struct sockaddr_in6 *sin6 =
        (const struct sockaddr_in6*)ipaddr_sockaddr(&addr);
    assert(ntohs(sin6->sin6_port) == 5555);
    port = ipaddr_port(&addr);
    assert(port == 5555);
    ipaddr_str(&addr, buf);
    assert(strcmp(buf, "::1") == 0);

    /* Looking an interface up when the system cannot list the interfaces is
       an error. Listing them needs a file descriptor on some systems. */
    struct rlimit nofile;
    rc = getrlimit(RLIMIT_NOFILE, &nofile);
    errno_assert(rc == 0);
    struct rlimit nofile_none = nofile;
    nofile_none.rlim_cur = 0;
    rc = setrlimit(RLIMIT_NOFILE, &nofile_none);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr, "no-such-interface", 5555, 0);
    assert(rc == -1 && (errno == EMFILE || errno == ENODEV));
    rc = setrlimit(RLIMIT_NOFILE, &nofile);
    errno_assert(rc == 0);

    /* Test IP address comparisons. */
    struct ipaddr addr1;
    struct ipaddr addr2;

    rc = ipaddr_local(&addr1, "1.2.3.4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "1.2.3.4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 0);
    assert(rc == 1);

    rc = ipaddr_local(&addr1, "1.2.3.4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "4.3.2.1", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 0);
    assert(rc == 0);

    rc = ipaddr_local(&addr1, "1.2.3.4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "1.2.3.4", 5556, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 0);
    assert(rc == 0);

    rc = ipaddr_local(&addr1, "1.2.3.4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "1.2.3.4", 5556, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 1);
    assert(rc == 1);

    rc = ipaddr_local(&addr1, "::1:2:3:4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "::1:2:3:4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 0);
    assert(rc == 1);

    rc = ipaddr_local(&addr1, "::1:2:3:4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "::4:3:2:1", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 0);
    assert(rc == 0);

    rc = ipaddr_local(&addr1, "::1:2:3:4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "::1:2:3:4", 5556, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 0);
    assert(rc == 0);

    rc = ipaddr_local(&addr1, "::1:2:3:4", 5556, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "::1:2:3:4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 1);
    assert(rc == 1);

    rc = ipaddr_local(&addr1, "::1:2:3:4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_local(&addr2, "1.2.3.4", 5555, 0);
    errno_assert(rc == 0);
    rc = ipaddr_equal(&addr1, &addr2, 0);
    assert(rc == 0);

    /* A name too long to be a host name is an error. So is, before the name
       gets looked at, the absence of DNS configuration on this machine. */
    char longname[300];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = 0;
    rc = ipaddr_remote(&addr, longname, 5555, IPADDR_IPV4, -1);
    assert(rc == -1 && (errno == ENAMETOOLONG || errno == ENOENT));

    return 0;
}
