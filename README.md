# imagegcc
requires GRX2.4.9 http://grx.gnu.de/download/index.html
bufr2wrk.py - script to process BUFR files
requires archive with zip files portN/YYMMDDHH.MMm

================================================================================
GRX 2.4.9 patches (2026 port to modern Linux)
================================================================================

The program links GRX 2.4.9, unpacked next to this directory as ../grx249.
Three files in that tree carry local patches.  Unpacking a fresh GRX tarball
resets them, and the library then has to be rebuilt FROM CLEAN, because the
affected types and tables are baked into every object file.  Each patched
file has a .orig-backup next to it.

Build order:

    cd ../grx249/contrib/grx249/src && make -f makefile.x11 clean
    cd ..                          && make -f makefile.x11 libs
    cd ../../../imagegcc           && ./make.sh

--------------------------------------------------------------------------------
1. makedefs.grx - build configuration                    needed by every build
--------------------------------------------------------------------------------

    HAVE_LIBPNG=n       ->  y       GrSaveContextToPng() is used
    NEED_ZLIB=n         ->  y
    BUILD_X86_64=n      ->  y       otherwise GRX compiles with -m32
    X11BASE=/usr/X11R6  ->  /usr    /usr/X11R6 has not existed for years
    X11LIBS             ->  -lX11   without -L: multiarch dir is searched anyway
    CCOPT               ->  added -std=gnu89 -fpermissive

GRX predates C99, and gcc 14 and later turn implicit declarations, implicit
int and incompatible pointer conversions into hard errors.  Without the
dialect flags GRX does not compile at all.

On a target that is neither i386 nor x86_64, also drop the -m64/-m32 from
the BUILD_X86_64 branch of that file: aarch64 gcc accepts neither flag.

--------------------------------------------------------------------------------
2. src/include/libgrx.h - 64 bit integer types             needed on aarch64
--------------------------------------------------------------------------------

    -#if defined(__alpha__) || (... _MIPS_SZLONG == 64) || defined(__x86_64__)
    +#if defined(__alpha__) || (... _MIPS_SZLONG == 64) || defined(__x86_64__) \
    + || defined(__aarch64__) || (defined(__SIZEOF_LONG__) && __SIZEOF_LONG__ == 8)
         #define GR_int32 int
         #define GR_int64 long
         #define GR_PtrInt long

GRX picks its integer types from a hardcoded list of 64 bit platforms that
names only alpha, mips64 and x86_64.  On any other LP64 target - aarch64,
for one - the #else branch makes GR_int32 a 64 bit long and GR_PtrInt a 32
bit int.  memfill.h builds the 24bpp fill pattern in a GR_int32u, so every
fill then writes twice the intended width.

    Symptom: filled areas come out in regular fine vertical stripes - the
    palette background, the grey "no data" region of the map - while lines,
    text and the map outlines look perfectly correct.  It is easy to
    mistake for a video or X11 problem; it is neither.

x86_64 was already in the list, so this patch changes nothing there.  The
extra __SIZEOF_LONG__ test covers riscv64, ppc64, s390x and the rest.

--------------------------------------------------------------------------------
3. src/vdrivers/vd_mem.c - 24bpp colour layout           needed by imageqt only
--------------------------------------------------------------------------------

    gr24ext:
    -    { 0, 0, 0 },                   /* color component bit positions */
    +    { 16, 8, 0 },

The memory video driver never filled in the red/green/blue bit positions
for its 24bpp mode, so GrAllocColor() shifts all three components to bit 0.

    Symptom: with the Qt front end every colour collapses - GrAllocColor
    (255,0,0) and (0,0,255) both return 0x0000ff and the whole map is drawn
    in shades of blue.

Only imageqt is affected: it is the sole caller of GrSetDriver("memory").
gen-bitmap uses plain RAM frame contexts and imagegcc uses the X11 driver,
so both are fine against a stock vd_mem.c.

--------------------------------------------------------------------------------
Checking a build
--------------------------------------------------------------------------------

    IMAGEQT_DEBUG=1 ./imageqt

prints how GRX laid out the surfaces.  On a healthy build both depths are
24 and each stride is exactly width*3:

    screen: 1920x1080 24 bpp stride 5760 base 0x...
    map   : 1500x1100 24 bpp stride 4500 base 0x... origin 100,13
    colors: red=0xff0000 green=0x00ff00 blue=0x0000ff (expect ...)

A wrong colors line means patch 3 is missing; stripes in the filled areas
with those three lines correct mean patch 2 is missing.

    IMAGEQT_SHOT=<file> ./imageqt -platform offscreen

saves a picture of the window and exits, for checking a remote machine
without a display.
