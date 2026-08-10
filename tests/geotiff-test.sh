#!/bin/sh
# Render the test fixture to GeoTIFF, one file per product family, and check
# each one.  The companion to the "render every product family" smoke test in
# .github/workflows/macos.yml, for the data output rather than the picture.
#
#   ./tests/geotiff-test.sh                 use ./gen-bitmap
#   BIN=dist/gen-bitmap ./tests/geotiff-test.sh
#
# Run it from the repository root, or from tests/ - it finds its way either
# way.  tests/data is built from the BUFR messages in tests/bufr, see
# tests/README; nothing here needs GDAL.

cd "$(dirname "$0")/.." || exit 1
BIN=${BIN:-./gen-bitmap}
OUT=${OUT:-tiffs}
CHECK="python3 tests/geotiff-check.py"

if [ ! -x "$BIN" ]; then
    echo "$BIN not found - build it first (./make.sh)" >&2
    exit 1
fi

# A separate path file, passed as an argument, so the tracked one still points
# at the real archive after a test run.  mapdir is char[80] in files.c, so a
# long absolute MAP path is silently truncated; keep it relative.
PATHFILE=paths-test
printf 'WRK RAB\nMAP tests/data\nCFG config\nGRF config\n' > "$PATHFILE"
mkdir -p "$OUT"
rc=0
LABELS=''      # what this run rendered, so stray files in $OUT do not count

# render <label> <expected nodata> <gen-bitmap args...>
render() {
    label=$1; nodata=$2; shift 2
    LABELS="$LABELS $label"
    rm -f "$OUT/$label.tif"
    echo "=== $label ($*)"
    if ! $BIN "$PATHFILE" "$@" "geotiff=$OUT/$label.tif" > "$OUT/$label.log" 2>&1; then
        echo "    FAIL: gen-bitmap exited non-zero"; cat "$OUT/$label.log"
        rc=1; return
    fi
    # the exit status does not report a frame that loaded nothing
    if grep -qE 'Cannot open zip file|no product in this file' "$OUT/$label.log"; then
        echo "    FAIL: did not load"; cat "$OUT/$label.log"; rc=1; return
    fi
    $CHECK "$OUT/$label.tif" --size 1500 --nodata "$nodata" --min-data 200 \
        || rc=1
}

#        label         nodata  product and frame
render reflectivity    254     time22:00 map1
render maxreflect      255     time22:00 map0
render zdr             121     time22:00 zdr2
render height          254     time22:00 top
render phenomena       254     time22:00 phenom
render rainfall        255     time22:00 sum1
render velocity        255     time10:50 vel1

# The Tyumen frame, which carries ten ZDR levels and ten reflectivity ones.
# --zdr-source compares the raster against the .wrk it was built from: the
# reprojection fills holes from the neighbours, and doing that in the byte
# rather than in the value invented readings of the opposite sign, which is
# how blue cells appeared inside convective cores.
render_zdr() {
    level=$1
    label="zdr_tyumen_$level"
    LABELS="$LABELS $label"
    rm -f "$OUT/$label.tif"
    echo "=== $label (time18:50 zdr$level)"
    if ! $BIN "$PATHFILE" "time18:50" "zdr$level" "geotiff=$OUT/$label.tif" \
            > "$OUT/$label.log" 2>&1; then
        echo "    FAIL: gen-bitmap exited non-zero"; cat "$OUT/$label.log"
        rc=1; return
    fi
    $CHECK "$OUT/$label.tif" --size 1500 --nodata 121 --min-data 200 \
        --zdr-source tests/data/port97/26081018.50m:4_dif_$level.wrk || rc=1
}

render_zdr 1
render_zdr 3
render_zdr 10

# Every product must come out different.  Identical files would mean the
# selection was ignored and one raster was written ten times.
echo "=== all ten differ"
if command -v md5 >/dev/null 2>&1; then HASH="md5 -q"; else HASH=md5sum; fi
files=''
for label in $LABELS; do files="$files $OUT/$label.tif"; done
n=$(echo $files | wc -w)
u=$($HASH $files 2>/dev/null | awk '{print $1}' | sort -u | wc -l)
echo "    $n files, $u distinct"
[ "$n" -eq 10 ] || { echo "    FAIL: expected 10 files"; rc=1; }
[ "$u" -eq "$n" ] || { echo "    FAIL: products are identical"; rc=1; }

# A product the frame does not carry must be refused, not quietly filled in
# with whatever else loaded - set_cur_map() falls back on screen, and a file
# named after a product it does not contain would be worse than no file.
echo "=== a product this frame does not carry is refused"
rm -f "$OUT/absent.tif"
if $BIN "$PATHFILE" time10:50 phenom "geotiff=$OUT/absent.tif" > "$OUT/absent.log" 2>&1; then
    echo "    FAIL: exited zero for a product that is not there"; rc=1
elif [ -f "$OUT/absent.tif" ]; then
    echo "    FAIL: wrote a file anyway"; rc=1
else
    echo "    refused, wrote nothing, exit non-zero"
fi

rm -f "$PATHFILE"
[ $rc = 0 ] && echo "GEOTIFF TESTS PASSED" || echo "GEOTIFF TEST FAILURE"
exit $rc
