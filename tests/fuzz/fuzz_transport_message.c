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
 * Fuzz harness for the transport message parsers.
 *
 * A message body received from a peer is attacker-controlled: it is
 * allocated at exactly header.len bytes and is not guaranteed to be
 * NUL-terminated or well-formed. This harness reconstructs that situation
 * (first input byte selects the message type, the rest becomes the body)
 * and exercises every accessor a service or the hub runs on a freshly
 * received message.
 *
 * Build with clang -fsanitize=fuzzer,address (see build-fuzz.sh), or with
 * -DFUZZ_STANDALONE for a plain driver that replays corpus files.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "transport_message.h"

/* transport_message.c references this counter (defined in base.c, hidden in
 * the shared library); provide an instance for the instrumented build */
volatile int activity_num = 0;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 1024 * 1024)
        return 0;

    static const _LSTransportMessageType types[] =
    {
        _LSTransportMessageTypeMethodCall,
        _LSTransportMessageTypeCancelMethodCall,
        _LSTransportMessageTypeReply,
        _LSTransportMessageTypeReplyWithFd,
        _LSTransportMessageTypeError,
        _LSTransportMessageTypeErrorUnknownMethod,
        _LSTransportMessageTypeSignal,
        _LSTransportMessageTypeSignalRegister,
        _LSTransportMessageTypeSignalUnregister,
        _LSTransportMessageTypeServiceUpSignal,
        _LSTransportMessageTypeServiceDownSignal,
        _LSTransportMessageTypeQueryNameReply,
        _LSTransportMessageTypeClientInfo,
        _LSTransportMessageTypeRequestName,
        _LSTransportMessageTypeNodeUp,
        _LSTransportMessageTypeQueryServiceStatus,
        _LSTransportMessageTypeQueryServiceCategory,
        _LSTransportMessageTypeAppendCategory,
    };

    _LSTransportMessageType type = types[data[0] % (sizeof(types) / sizeof(types[0]))];
    const uint8_t *body = data + 1;
    size_t body_len = size - 1;

    _LSTransportMessage *msg = _LSTransportMessageNewRef(body_len);
    if (!msg)
        return 0;

    _LSTransportMessageSetType(msg, type);
    if (body_len)
        memcpy(_LSTransportMessageGetBody(msg), body, body_len);

    /* string accessors: all must survive unterminated / truncated bodies */
    volatile const char *s;
    s = _LSTransportMessageGetPayload(msg);   (void)s;
    s = _LSTransportMessageGetMethod(msg);    (void)s;
    s = _LSTransportMessageGetCategory(msg);  (void)s;
    s = _LSTransportMessageGetAppId(msg);     (void)s;

    /* argument iterator: lengths inside the body are attacker-controlled */
    _LSTransportMessageIter iter;
    _LSTransportMessageIterInit(msg, &iter);
    int guard = 0;
    while (_LSTransportMessageIterHasNext(&iter) && guard++ < 4096)
    {
        const char *str = NULL;
        int32_t i32 = 0;
        int64_t i64 = 0;
        bool b = false;

        _LSTransportMessageGetString(&iter, &str);
        (void)str;
        _LSTransportMessageGetInt32(&iter, &i32);
        _LSTransportMessageGetInt64(&iter, &i64);
        _LSTransportMessageGetBool(&iter, &b);

        if (!_LSTransportMessageIterNext(&iter))
            break;
    }

    _LSTransportMessageUnref(msg);
    return 0;
}

#ifdef FUZZ_STANDALONE
/* Plain driver: replay the files given on the command line (or stdin). */
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        uint8_t buf[1024 * 1024];
        size_t n = fread(buf, 1, sizeof(buf), stdin);
        LLVMFuzzerTestOneInput(buf, n);
        return 0;
    }

    for (int i = 1; i < argc; ++i)
    {
        FILE *f = fopen(argv[i], "rb");
        if (!f)
            continue;
        uint8_t *buf = malloc(1024 * 1024);
        size_t n = fread(buf, 1, 1024 * 1024, f);
        fclose(f);
        fprintf(stderr, "replaying %s (%zu bytes)\n", argv[i], n);
        LLVMFuzzerTestOneInput(buf, n);
        free(buf);
    }
    fprintf(stderr, "done\n");
    return 0;
}
#endif
