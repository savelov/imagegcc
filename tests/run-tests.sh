#!/bin/sh
# Build and run the GRX regression probes.
#
#   ./run-tests.sh            build for this host and run
#   ./run-tests.sh --windows  cross build with mingw and run under wine
#
# On an MSYS2/MinGW shell the host *is* Windows, so the first form builds
# native .exe and runs them directly.  Exits non-zero if any probe reports
# a bad result, which is what makes this usable as a CI gate.
#
# GRX defaults to ../../grx249/contrib/grx249; override with GRX=...

GRX=${GRX:-../../grx249/contrib/grx249}
TESTS="sttzero alloc-colour colour-readback fill-runs-8bpp fill-runs-24bpp"

case "$1$(uname -s)" in
    --windows*)          CC=x86_64-w64-mingw32-gcc; WIN=1; RUN=wine ;;
    *MINGW*|*MSYS*)      CC=gcc;                    WIN=1; RUN=     ;;
    *)                   CC=gcc;                    WIN=0; RUN=     ;;
esac

if [ "$WIN" = 1 ]; then
    LIB="$GRX/lib/win32/libgrx20.a";  SYSLIBS="-lgdi32 -luser32"; EXT=.exe
else
    LIB="$GRX/lib/unix/libgrx20X.a";  SYSLIBS="-lX11 -lz -lpng -lm"; EXT=
fi

if [ ! -f "$LIB" ]; then
    echo "$LIB not found - build GRX first (see README)" >&2
    exit 1
fi

out=$(mktemp); rc=0
for t in $TESTS; do
    echo "=== $t"
    $CC -g -std=gnu99 -w -o "/tmp/$t$EXT" "$t.c" \
        -I "$GRX/include" -I "$GRX/src/include" "$LIB" $SYSLIBS || { rc=1; continue; }
    $RUN "/tmp/$t$EXT" 2>/dev/null | tee -a "$out" || rc=1
done

echo "=== clear-overrun"
$CC -g -std=gnu99 -w -o "/tmp/clear-overrun$EXT" clear-overrun.c \
    -I "$GRX/include" "$LIB" $SYSLIBS || rc=1
for wh in "64 1" "100 1113" "1500 1100"; do
    $RUN "/tmp/clear-overrun$EXT" $wh 2>/dev/null | tee -a "$out" || { echo "  crashed at $wh"; rc=1; }
done

# a probe reports trouble with one of these words
if grep -qE "WRONG|OVERRUN|FAIL" "$out"; then rc=1; fi
if ! grep -q "non-zero after sttzero: 0" "$out"; then rc=1; fi
rm -f "$out"

[ $rc = 0 ] && echo "ALL PROBES PASSED" || echo "PROBE FAILURE"
exit $rc
