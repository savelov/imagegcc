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

================================================================================
Building for Windows
================================================================================

MinGW-w64 under MSYS2, 64 bit only: the port bitmask is an `unsigned __int128`,
which 32 bit mingw does not have.

    pacman -S make patch mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf \
              mingw-w64-x86_64-zlib mingw-w64-x86_64-libpng \
              mingw-w64-x86_64-proj mingw-w64-x86_64-minizip \
              mingw-w64-x86_64-qt5-base

GRX builds against its own Win32 GDI driver rather than X11, and make.sh picks
the right library and system libraries from `uname -s`:

    make -C <grx-tree> -f makefile.w32 libs      # produces lib/win32/libgrx20.a
    GRX=<grx-tree> ./make.sh

The same three patches in grx-patches/ apply.  Patch 2 (libgrx.h) is not
strictly required here - Windows x64 is LLP64, so `long` is 32 bit and GRX's
`#else` branch happens to give the right GR_int32 - but note that GR_PtrInt
stays `int` there while pointers are 64 bit.  That is only used for alignment
tests, where the truncated low bits still come out right, so it works; it is
worth fixing properly if anything else starts using it.

What is expected to work, and what is not:

  gen-bitmap   batch renderer, no window          - the safe target
  imagegcc     -DGUI, opens a native GDI window through GRX's win32 driver
  imageqt      Qt front end; GRX renders into memory, so no GRX window driver
               is involved at all

Windows specifics already handled in the sources: getch() uses <conio.h>
_getch() instead of termios (compat.c).  dirent.h, unistd.h and sys/stat.h all
exist in mingw-w64, and forward slashes work in paths, so the rest of the
POSIX use in the program needs no changes.

Continuous integration
----------------------

.github/workflows/windows.yml builds all of the above on every push and
uploads the binaries, their runtime DLLs and the config/ directory as an
artifact.

It downloads GRX from upstream:

    http://grx.gnu.de/download/grx249.tar.gz
    sha256 a899956b3ee46492696114d220431405320c64c1f6f058fdfc2b4d6a2beae786

Only http is served, so the digest is checked after the download.  To use a
different source - a mirror, or a copy kept as a release asset - set the
repository variables GRX_URL and GRX_SHA256 (Settings -> Secrets and variables
-> Actions -> Variables).  A tree committed as third_party/grx249 is used in
preference to any download.

Note that the upstream tarball ships HAVE_UNIX_TOOLS=n, so its makefiles emit
DOS commands ("if exist ... del ...") that no unix shell can run - including
MSYS2's.  Patch 1 sets it to y, which is why GRX has to be patched before it
is built even on Windows.

What has actually been tested
-----------------------------

Cross compiled from Linux with x86_64-w64-mingw32-gcc 13:

  * GRX 2.4.9 builds for Windows from the upstream tarball with the three
    patches applied - lib/win32/libgrx20.a, no errors
  * every .c file of this program compiles for Windows, with no implicit
    declarations and no missing headers
  * unsigned __int128, which the port bitmask needs, compiles and links

Not yet verified: linking against the Windows builds of zlib, libpng, PROJ,
minizip and Qt5.  That is what the CI run exercises.

The patches are generated against the upstream grx249.tar.gz above.  The
DJGPP-style grx249s.zip distribution has some makedefs.grx defaults set
differently; applying these patches to it reports one hunk as already
applied, which is harmless (the workflow passes -N for that reason).
