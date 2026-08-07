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
#
# clang accepts -fpermissive but ignores it, so it needs the warnings named
# one by one instead.  That is the compiler used for windows on arm64.
CC=${CC:-gcc}
CXX=${CXX:-g++}
if $CC --version 2>/dev/null | grep -qi clang; then
    PERMISSIVE="-Wno-implicit-int -Wno-implicit-function-declaration \
                -Wno-int-conversion -Wno-incompatible-pointer-types \
                -Wno-return-type -Wno-deprecated-non-prototype"
else
    PERMISSIVE="-fpermissive -Wno-implicit-int -Wno-implicit-function-declaration"
fi
CFLAGS="-g -std=gnu89 $PERMISSIVE -fcommon"

SRC="image.c showmap.c coord.c showdata.c files.c grafs.c archive.c window.c
     vert.c crosssect.c palette.c geotiff.c compat.c proj_compat.c"

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

LIBS="$GRX_LIB -lz -lpng -ltiff $WINDOW_LIBS -lm $PROJ_LIBS $MINIZIP_LIBS"

rm -f gen-bitmap imagegcc gen-bitmap.exe imagegcc.exe

echo "building gen-bitmap ..."
$CC $CFLAGS      -o gen-bitmap -I $GRX/include/ $SRC $LIBS

echo "building imagegcc ..."
$CC $CFLAGS -DGUI -o imagegcc  -I $GRX/include/ $SRC $LIBS

# imageqt: same drawing code, but rendered into memory and shown by Qt.
# Qt6 first, then Qt5 - MSYS2 ships only Qt6 for CLANGARM64, while the
# mingw64 and debian toolchains still carry Qt5.  Force one with QT=5 or
# QT=6.  Skipped entirely when neither is present, so the other two
# targets still build.
QTPKG=
case "${QT:-auto}" in
    6) pkg-config --exists Qt6Widgets 2>/dev/null && QTPKG=Qt6Widgets ;;
    5) pkg-config --exists Qt5Widgets 2>/dev/null && QTPKG=Qt5Widgets ;;
    *) if   pkg-config --exists Qt6Widgets 2>/dev/null; then QTPKG=Qt6Widgets
       elif pkg-config --exists Qt5Widgets 2>/dev/null; then QTPKG=Qt5Widgets
       fi ;;
esac

if [ -n "$QTPKG" ]; then
    QTCORE=${QTPKG%Widgets}Core
    # moc lives in host_bins for Qt5 and in libexec for Qt6, and neither
    # variable is reliably set, so try the usual places in turn
    MOC=${MOC:-}
    for cand in "$MOC" \
        "$(pkg-config --variable=host_bins  $QTCORE 2>/dev/null)/moc" \
        "$(pkg-config --variable=libexecdir $QTCORE 2>/dev/null)/moc" \
        "$(pkg-config --variable=prefix $QTCORE 2>/dev/null)/share/${QTCORE%Core}/bin/moc" \
        "$(pkg-config --variable=prefix $QTCORE 2>/dev/null)/share/qt/libexec/moc" \
        /usr/lib/qt6/libexec/moc moc-qt6 moc6 moc; do
        [ -z "$cand" ] && continue
        [ -x "$cand" ] && MOC=$cand && break
        command -v "$cand" >/dev/null 2>&1 && MOC=$cand && break
    done
    [ -z "$MOC" ] && { echo "make.sh: $QTPKG found but no moc; skipping imageqt" >&2; QTPKG=; }
fi

if [ -n "$QTPKG" ]; then
    echo "building imageqt ($QTPKG) ..."
    rm -f imageqt imageqt.exe qtmain.moc
    OBJDIR=.qtobj
    mkdir -p $OBJDIR

    for src in $SRC qt_bridge.c; do
        $CC $CFLAGS -DQTGUI -I $GRX/include/ -c $src -o $OBJDIR/${src%.c}.o
    done

    "$MOC" qtmain.cpp -o qtmain.moc
    # Qt6 requires C++17 and Qt5 is happy with it; gcc defaults to gnu++17
    # but apple clang does not, so say it explicitly
    $CXX -g -fPIC -std=c++17 $(pkg-config --cflags $QTPKG) -c qtmain.cpp -o $OBJDIR/qtmain.o
    $CXX -o imageqt $OBJDIR/*.o $LIBS $(pkg-config --libs $QTPKG)
else
    echo "skipping imageqt: no Qt5 or Qt6 found" >&2
fi

echo "done"
