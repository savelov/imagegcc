#!/bin/sh
# Build the image tools against GRX.
#
# Requires the GRX library to be built first:
#   unix:     cd $GRX && make -f makefile.x11 libs      (X11 driver)
#   windows:  cd $GRX/src && make -f makefile.w32 libs  (native GDI driver)
#
# Targets:
#   gen-bitmap  - batch bitmap generator (no window)
#   imagegcc    - interactive viewer (-DGUI), X11 or a native window
#   imageqt     - Qt front end, built when Qt5 is present

set -e

GRX=${GRX:-../grx249/contrib/grx249}

# GRX is built per platform and its window system differs with it.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        GRX_LIB="$GRX/lib/win32/libgrx20.a"
        WINDOW_LIBS="-lgdi32 -luser32"
        ;;
    *)
        GRX_LIB="$GRX/lib/unix/libgrx20X.a"
        WINDOW_LIBS="-lX11"
        ;;
esac

if [ ! -f "$GRX_LIB" ]; then
    echo "make.sh: $GRX_LIB not found - build GRX first (see the header above)" >&2
    exit 1
fi

# GCC 14+ rejects K&R style code by default; keep the old dialect and
# downgrade the new hard errors to warnings.  -fcommon (the default before
# GCC 10) is what lets the globals this code declares in several files at
# once resolve to one object, the way they always have.
CFLAGS="-g -std=gnu89 -fpermissive -fcommon -Wno-implicit-int -Wno-implicit-function-declaration"

SRC="image.c showmap.c coord.c showdata.c files.c grafs.c archive.c window.c
     vert.c compat.c proj_compat.c"

# PROJ: use the development package when it is installed, otherwise link the
# runtime library directly (proj_compat.c declares what it needs).
if pkg-config --exists proj 2>/dev/null; then
    PROJ_LIBS=$(pkg-config --libs proj)
else
    PROJ_LIBS=$(ls /usr/lib/*/libproj.so.[0-9]* /usr/lib/libproj.so.[0-9]* \
                   /usr/local/lib/libproj.so.[0-9]* 2>/dev/null | head -n 1)
    if [ -z "$PROJ_LIBS" ]; then
        PROJ_LIBS=-lproj                      # let the linker look
    fi
fi

# minizip: the zip reading used to be a copy of minizip inside this project,
# it is the system library now.  Same fallback as PROJ.
if pkg-config --exists minizip 2>/dev/null; then
    MINIZIP_LIBS=$(pkg-config --libs minizip)
else
    MINIZIP_LIBS=$(ls /usr/lib/*/libminizip.so.[0-9]* /usr/lib/libminizip.so.[0-9]* \
                      /usr/local/lib/libminizip.so.[0-9]* 2>/dev/null | head -n 1)
    if [ -z "$MINIZIP_LIBS" ]; then
        MINIZIP_LIBS=-lminizip
    fi
fi

LIBS="$GRX_LIB -lz -lpng $WINDOW_LIBS -lm $PROJ_LIBS $MINIZIP_LIBS"

rm -f gen-bitmap imagegcc gen-bitmap.exe imagegcc.exe

echo "building gen-bitmap ..."
gcc $CFLAGS      -o gen-bitmap -I $GRX/include/ $SRC $LIBS

echo "building imagegcc ..."
gcc $CFLAGS -DGUI -o imagegcc  -I $GRX/include/ $SRC $LIBS

# imageqt: same drawing code, but rendered into memory and shown by Qt.
# Skipped when Qt5 is not installed, so the other two targets still build.
if pkg-config --exists Qt5Widgets 2>/dev/null; then
    echo "building imageqt ..."
    rm -f imageqt imageqt.exe qtmain.moc
    OBJDIR=.qtobj
    mkdir -p $OBJDIR

    for src in $SRC qt_bridge.c; do
        gcc $CFLAGS -DQTGUI -I $GRX/include/ -c $src -o $OBJDIR/${src%.c}.o
    done

    moc qtmain.cpp -o qtmain.moc
    g++ -g -fPIC $(pkg-config --cflags Qt5Widgets) -c qtmain.cpp -o $OBJDIR/qtmain.o
    g++ -o imageqt $OBJDIR/*.o $LIBS $(pkg-config --libs Qt5Widgets)
else
    echo "skipping imageqt: Qt5Widgets not found (install qtbase5-dev)" >&2
fi

echo "done"
