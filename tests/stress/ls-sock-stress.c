// Copyright (c) 2026 LuneOS project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

/*
 * Hub-socket robustness stress tool.
 *
 * Connects to the ls-hubd unix socket and throws hostile traffic at it:
 * random bytes, valid-looking headers with absurd lengths, truncated
 * messages, and rapid connect/disconnect churn. The hub must survive all
 * of it; run alongside ls-hub-stress.sh which checks the daemon health.
 *
 * Usage: ls-sock-stress <socket-path> [iterations]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

/* mirrors struct LSTransportHeader (transport_message.h):
 * { unsigned long len; LSMessageToken token; int type; bool is_public_bus; } */
struct header {
    unsigned long len;
    unsigned long token;
    int32_t type;
    uint8_t is_public_bus;
};

static int hub_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void send_bytes(int fd, const void *buf, size_t len)
{
    ssize_t unused = send(fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    (void)unused;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <hub-socket-path> [iterations]\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];
    long iterations = argc > 2 ? atol(argv[2]) : 2000;

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    long connected = 0, failed = 0;
    unsigned char buf[4096];

    for (long i = 0; i < iterations; ++i)
    {
        int fd = hub_connect(path);
        if (fd < 0)
        {
            ++failed;
            /* if the hub ever stops accepting, that's the finding */
            usleep(1000);
            continue;
        }
        ++connected;

        switch (i % 5)
        {
        case 0: /* pure random garbage */
        {
            size_t n = 1 + (size_t)(rand() % sizeof(buf));
            for (size_t j = 0; j < n; ++j) buf[j] = (unsigned char)rand();
            send_bytes(fd, buf, n);
            break;
        }
        case 1: /* plausible header, absurd length */
        {
            struct header h;
            memset(&h, 0, sizeof(h));
            h.len = 0xFFFFFFFFu >> (rand() % 8);
            h.type = (uint16_t)(rand() % 64);
            h.token = (uint64_t)rand();
            send_bytes(fd, &h, sizeof(h));
            break;
        }
        case 2: /* valid-size header, truncated body, then hang up */
        {
            struct header h;
            memset(&h, 0, sizeof(h));
            h.len = 1024;
            h.type = (uint16_t)(rand() % 64);
            send_bytes(fd, &h, sizeof(h));
            send_bytes(fd, buf, (size_t)(rand() % 512));
            break;
        }
        case 3: /* header with zero-length body for every type */
        {
            struct header h;
            memset(&h, 0, sizeof(h));
            h.len = 0;
            h.type = (uint16_t)(i % 64);
            send_bytes(fd, &h, sizeof(h));
            break;
        }
        case 4: /* single bytes with delays: slow-loris style */
        {
            struct header h;
            memset(&h, 0, sizeof(h));
            h.len = 64;
            h.type = 1;
            const unsigned char *p = (const unsigned char *)&h;
            for (size_t j = 0; j < sizeof(h) && j < 8; ++j)
                send_bytes(fd, p + j, 1);
            break;
        }
        }

        /* sometimes read a reply, sometimes just slam the door */
        if (rand() % 2)
        {
            struct timeval tv = { 0, 20000 };
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ssize_t unused = recv(fd, buf, sizeof(buf), 0);
            (void)unused;
        }
        close(fd);
    }

    printf("ls-sock-stress: %ld iterations, %ld connected, %ld connect failures\n",
           iterations, connected, failed);
    return failed > iterations / 10 ? 2 : 0;
}
