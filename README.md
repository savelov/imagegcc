# imagegcc
requires GRX2.4.9 http://grx.gnu.de/download/index.html
bufr2wrk.py - script to process BUFR files
requires archive with zip files portN/YYMMDDHH.MMm

================================================================================
Products and palettes
================================================================================

Every map file bufr2wrk.py writes can be displayed:

    4_dbz_0.wrk        maximum reflectivity, and the rain rate derived from it
    4_dbz_1..15.wrk    reflectivity, constant altitude levels 1 to 15
    4_dif_1..10.wrk    differential reflectivity (ZDR)
    4_vel_1..10.wrk    radial (Doppler) velocity
    4_heigh.wrk        echo top height
    4_myavl.wrk        phenomena
    1..5_summ.wrk      rainfall over 1, 3, 6, 12 and 24 hours

Phenomena come from 4_myavl.wrk rather than 4_storm.wrk.  The two hold the same
field in different codings: myavl stores the phenomenon code itself (0 to 19),
which is what the palette is keyed on, while storm.wrk carries the older
0/3/5/../60 severity scale.

--------------------------------------------------------------------------------
No-data bytes
--------------------------------------------------------------------------------

A point the radar has no reading for is not marked the same way in every
product, so maps[] in files.c carries the byte per product.  These are measured
from the archive, not assumed: take a pixel that 4_dbz_1.wrk marks 254 (so it is
known to be outside the coverage) and see what the other products put there.
Over 300 random archives:

    4_dbz_1..15, 4_heigh, 4_myavl   254   bufr2wrk marks these itself
    4_dif_*                         121   debufr's wrapped value table
    4_vel_*                         255   bufr2wrk; 178 in older files, below
    4_dbz_0 (rain rate)             221   or 164 - see below
    N_summ                           14

The last two have no marker of their own: bufr2wrk.py gives reflectivity, ZDR
and velocity a dedicated byte, but for the rain rate and the sums a missing
point simply saturates the top of the run length table.  Where that lands
depends on how wide the source field is - 221 (1431 mm/h) on the DMRL radars and
164 (93 mm/h) on the AKSOPRI ones, 184 of the 210 sampled archives.  Both are
read as no data; 164 is folded onto 221 as the file is read, so everything after
that has one marker to test.  The cost is the 93-97 mm/h bin of the rain rate,
which no real echo is likely to land in and which the DMRL radars cannot produce
at all.  Untreated, the whole out-of-range area of a map reads as a downpour -
1467 mm/h, or 73 dBZ when the same file is shown as maximum reflectivity.

Known and not handled: 4_vel_* in the older archives uses 178, which debufr
produced by wrapping the BUFR missing value.  That is a real 25.5 m/s reading,
so it cannot be folded away without punching holes in genuine velocity fields -
which is exactly why bufr2wrk.py moved to a dedicated 255.  Files written by
bufr2wrk are correct; older ones show a 25.5 m/s ring outside the coverage.

Keys, since there are more products than letters:

    0..9   reflectivity levels 0 to 9     r   next reflectivity level
    p      rain rate                      d   next ZDR level
    h      echo top height                v   next velocity level
    s      phenomena                      q   next rainfall sum
    [ ]    previous / next level of the family on screen

gen-bitmap picks its product from the command line: `map<N>` for a reflectivity
level (this used to be a raw index into the product table, which moved with the
table - it is the level now, the same number the digit keys use), `zdr<N>`,
`vel<N>`, `sum<hours>`, `top`, `phenom` or `rain`.

--------------------------------------------------------------------------------
Colours
--------------------------------------------------------------------------------

Colours come from the pycao palettes - the same ones the web side serves, see
cao/palettes and geotiff.py.  mkpalettes.py walks the 256 possible .wrk bytes
per product, converts each to a physical value with the cao.conversion function
that byte scale belongs to, looks the value up in the pycao palette and writes
the result to config/palettes/<product>.pal: a 256 entry colour table plus the
legend, in CP866 like every other text file here.

    ./mkpalettes.py [--pycao ../pycao_numba] [--out config/palettes]

It needs the cao package, but not numba - it shims the decorators cao.conversion
uses.  The .pal files are committed, so a build machine needs neither.

The program reads them at run time; Python stays the only definition of the
colours.  To change a palette, change it in pycao and regenerate.

config/palette is still read, for the sixteen user interface colours - frames,
text, cursor, background.  It is no longer a limit on anything the map shows.

Two of those sixteen are a matched pair, and the whole program depends on it:

    entry 0    paper - panel backgrounds, window fills, the cross section
    entry 15   ink   - all text, frames, axes, the cursor, motion vectors,
                       and the default colour of the graf.k* overlays

The overlays are drawn in it too: graf.k2 opens with `COL 15` for the city
names and the region borders, and draw_tlo() starts every pass at 15 - its
comment has always called that "white".  It was black, because the map's own
no-data used to be the light grey of entry 0 and black ink read well on it.
The palettes make no-data black, so the pair is the other way round now: entry
0 is black and entry 15 is white.  Change them together or the overlays, the
legend or the archive browser go dark on dark.
config/porog.* are no longer read at all: they fed the old fifteen level
config/thresh.* tables, which the palettes replace.

--------------------------------------------------------------------------------
The 16 colour limit, and where it was
--------------------------------------------------------------------------------

gen-bitmap used to draw into a bare RAM8 frame context with no GrSetMode call.
GRX then leaves its colour table at the sixteen entries it starts life with and
GrAllocColor gives up after those, so the batch renderer really did have only
sixteen colours to draw with.  It takes the same route imageqt does now - the
memory driver in a truecolor mode - which also means GRX patch 3 below is needed
for gen-bitmap and not only for imageqt.

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
3. src/vdrivers/vd_mem.c - 24bpp colour layout    needed by imageqt, gen-bitmap
--------------------------------------------------------------------------------

    gr24ext:
    -    { 0, 0, 0 },                   /* color component bit positions */
    +    { 16, 8, 0 },

The memory video driver never filled in the red/green/blue bit positions
for its 24bpp mode, so GrAllocColor() shifts all three components to bit 0.

    Symptom: with the Qt front end every colour collapses - GrAllocColor
    (255,0,0) and (0,0,255) both return 0x0000ff and the whole map is drawn
    in shades of blue.

imageqt and gen-bitmap are affected: both call GrSetDriver("memory") - gen-bitmap
does so to escape the sixteen colour default described above.  imagegcc uses the
X11 driver and is fine against a stock vd_mem.c.

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
