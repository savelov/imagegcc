#!/bin/sh
# Build and run the GRX regression probes.
#
# Each one checks a primitive that was found broken on Windows while it
# worked on Linux, so they are meant to be run on both and compared:
#
#   ./run-tests.sh            build for the host and run
#   ./run-tests.sh --windows  cross build with mingw and run under wine
#
# GRX defaults to ../../grx249/contrib/grx249; override with GRX=...

set -e
GRX=${GRX:-../../grx249/contrib/grx249}
TESTS="sttzero alloc-colour colour-readback fill-runs-8bpp fill-runs-24bpp"

if [ "$1" = "--windows" ]; then
    CC=x86_64-w64-mingw32-gcc
    LIB="$GRX/lib/win32/libgrx20.a"
    SYSLIBS="-lgdi32 -luser32"
    RUN="wine"
    EXT=".exe"
else
    CC=gcc
    LIB="$GRX/lib/unix/libgrx20X.a"
    SYSLIBS="-lX11 -lz -lpng -lm"
    RUN=""
    EXT=""
fi

if [ ! -f "$LIB" ]; then
    echo "$LIB not found - build GRX first (see the README)" >&2
    exit 1
fi

for t in $TESTS; do
    echo "=== $t"
    $CC -g -std=gnu99 -w -o "/tmp/$t$EXT" "$t.c" \
        -I "$GRX/include" -I "$GRX/src/include" "$LIB" $SYSLIBS
    $RUN "/tmp/$t$EXT" || echo "  (exited non-zero)"
done

# clear-overrun takes a geometry, so drive it over a few
echo "=== clear-overrun"
$CC -g -std=gnu99 -w -o "/tmp/clear-overrun$EXT" clear-overrun.c \
    -I "$GRX/include" "$LIB" $SYSLIBS
for wh in "64 1" "100 1113" "1500 1100"; do
    $RUN "/tmp/clear-overrun$EXT" $wh || echo "  (crashed at $wh)"
done
