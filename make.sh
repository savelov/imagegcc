#!/bin/sh
# Build the X11 (GRX) version of the image tools.
#
# Requires the GRX library to be built first:
#   cd ../grx249/contrib/grx249 && make -f makefile.x11 libs
#
# Targets:
#   gen-bitmap  - batch bitmap generator (no window)
#   imagegcc    - interactive X11 viewer (-DGUI)

set -e

GRX=../grx249/contrib/grx249

# GCC 14+ rejects K&R style code by default; keep the old dialect and
# downgrade the new hard errors to warnings.  -fcommon (the default before
# GCC 10) is what lets the globals this code declares in several files at
# once resolve to one object, the way they always have.
CFLAGS="-g -std=gnu89 -fpermissive -fcommon -Wno-implicit-int -Wno-implicit-function-declaration"

SRC="image.c showmap.c coord.c showdata.c files.c grafs.c archive.c window.c
     unzip.c ioapi.c vert.c compat.c proj_compat.c"

# PROJ: use the development package when it is installed, otherwise link the
# runtime library directly (proj_compat.c declares what it needs).
if pkg-config --exists proj 2>/dev/null; then
    PROJ_LIBS=$(pkg-config --libs proj)
else
    PROJ_LIBS=$(ls /usr/lib/*/libproj.so.[0-9]* /usr/lib/libproj.so.[0-9]* \
                   /usr/local/lib/libproj.so.[0-9]* 2>/dev/null | head -n 1)
    if [ -z "$PROJ_LIBS" ]; then
        echo "make.sh: no PROJ library found - install libproj-dev or libproj25" >&2
        exit 1
    fi
fi

LIBS="$GRX/lib/unix/libgrx20X.a -lz -lpng -lX11 -lm $PROJ_LIBS"

rm -f gen-bitmap imagegcc

echo "building gen-bitmap ..."
gcc $CFLAGS      -o gen-bitmap -I $GRX/include/ $SRC $LIBS

echo "building imagegcc ..."
gcc $CFLAGS -DGUI -o imagegcc  -I $GRX/include/ $SRC $LIBS

echo "done"
