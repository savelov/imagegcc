#!/bin/sh
# Build with AddressSanitizer and UndefinedBehaviorSanitizer and put the test
# fixture through the batch renderer.  Every render below already runs in CI
# for its output; this runs the same code for what it does to memory.
#
#   GRX=../grx249 ./tests/sanitize-test.sh
#
# Note it overwrites ./gen-bitmap and ./imagegcc with sanitized binaries -
# they are several times slower and depend on libasan, so rebuild with plain
# ./make.sh afterwards if you meant to keep them.
#
# The cross= renders are the point of the exercise.  vert.c draws the vertical
# section, and until cross= existed the only way in was two mouse clicks on the
# map, so nothing headless had ever executed a line of it.  It held a read of
# tens of kB past a malloc that faulted on Windows and passed unnoticed on
# glibc, whose arenas are large enough to swallow it.  ASan does not care how
# large the arena is.

cd "$(dirname "$0")/.." || exit 1

SAN=1 ./make.sh || { echo "sanitizer build failed" >&2; exit 1; }

# A path file of its own, passed as an argument, so the tracked one still
# points at the real archive after a run.  mapdir is char[80] in files.c, so a
# long absolute MAP path is silently truncated; keep it relative.
PATHFILE=paths-san
printf 'WRK RAB\nMAP tests/data\nCFG config\nGRF config\n' > "$PATHFILE"

# The program frees very little and never meant to - it renders and exits.
# Leak reports would bury the memory errors this is looking for.
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
export ASAN_OPTIONS

rc=0
LOG=sanitize.log

run() {
    printf '  %-34s ' "$*"
    if ./gen-bitmap "$PATHFILE" "$@" > "$LOG" 2>&1; then
        # -fno-sanitize-recover=all makes a finding a non-zero exit, so this
        # is belt and braces - but a report that somehow did not is still a
        # failure, not a curiosity.
        if grep -qE 'runtime error|AddressSanitizer' "$LOG"; then
            echo "FAIL (reported, exited 0)"
            grep -E 'runtime error|ERROR: AddressSanitizer' "$LOG" | head -5
            rc=1
        else
            echo ok
        fi
    else
        echo "FAIL (exit $?)"
        # From the top: a sanitizer report puts what happened on its first
        # line and the stack under it, so tailing the log hides the answer.
        head -40 "$LOG"
        rc=1
    fi
}

echo "== renders"
run time22:00 map1
run time22:00 map0
run time22:00 zdr2
run time22:00 top
run time22:00 geotiff=sanitize.tif

echo "== vertical sections"
# Short, long and both diagonals: the length decides vert_size, which decides
# every buffer in vert.c, so one cut proves very little.
run time22:00 cross=40,40,60,60
run time22:00 cross=5,5,95,95
run time22:00 cross=95,5,5,95
run time22:00 cross=5,50,95,50
run time22:00 cross=50,5,50,95

rm -f "$PATHFILE" "$LOG" sanitize.tif cross.png

if [ $rc -eq 0 ]; then
    echo "sanitizers clean"
else
    echo "sanitizers found something" >&2
fi
exit $rc
