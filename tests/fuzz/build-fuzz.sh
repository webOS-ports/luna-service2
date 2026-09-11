#!/bin/bash
# Build the transport-message fuzz harness with host clang (libFuzzer+ASan)
# against an OE recipe sysroot and a cmake build directory of this tree.
#
# Usage:
#   SYSROOT=<recipe-sysroot> BUILDDIR=<cmake-build-dir> ./build-fuzz.sh
#
# The harness links against the libluna-service2.so from BUILDDIR, so the
# library code itself is exercised (uninstrumented; the ASan interceptors
# still catch heap overflows and UAF in it).
set -e
cd "$(dirname "$0")"
SRC=$(cd ../.. && pwd)
: "${SYSROOT:?set SYSROOT to an x86-64 recipe-sysroot}"
: "${BUILDDIR:?set BUILDDIR to an x86-64 cmake build dir of this tree}"
: "${CLANG:=clang}"

CFLAGS="-g -O1 -fno-omit-frame-pointer --sysroot=$SYSROOT
  -I$SRC/src/libluna-service2
  -I$SRC/include/public
  -I$SRC/include/private
  -isystem $SYSROOT/usr/include
  -isystem $SYSROOT/usr/include/glib-2.0
  -isystem $SYSROOT/usr/lib/glib-2.0/include"

# the OE sysroot keeps gcc's crt files under usr/lib/<triple>/<version>
GCCDIR=$(ls -d "$SYSROOT"/usr/lib/*-linux*/[0-9]* 2>/dev/null | head -1)

LDFLAGS="-B$GCCDIR -L$GCCDIR -L$BUILDDIR/src/libluna-service2 -lluna-service2
  -L$SYSROOT/usr/lib -lglib-2.0 -lPmLogLib
  -Wl,-rpath,$BUILDDIR/src/libluna-service2:$SYSROOT/usr/lib:$SYSROOT/lib
  -Wl,--dynamic-linker=$SYSROOT/usr/lib/ld-linux-x86-64.so.2"

# compile the parser under test directly into the fuzzer so libFuzzer gets
# real coverage feedback for it (remaining symbols come from the shared lib)
$CLANG $CFLAGS -fsanitize=fuzzer,address \
    -include "$BUILDDIR/Configured/webospaths.h" -DUSE_PMLOG_DECLARATION -DSECURITY_COMPATIBILITY -DLSHANDLE_CHECK \
    fuzz_transport_message.c "$SRC/src/libluna-service2/transport_message.c" \
    $LDFLAGS -o fuzz_transport_message

$CLANG $CFLAGS -DFUZZ_STANDALONE fuzz_transport_message.c \
    $LDFLAGS -o fuzz_transport_message_replay

echo "built: fuzz_transport_message (libFuzzer+ASan), fuzz_transport_message_replay (plain)"
echo "run:   ./fuzz_transport_message -max_len=65536 corpus/"
