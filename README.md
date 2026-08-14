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
    4_dbz_0 (rain rate)             255   221 or 164 in older files, below
    N_summ                          255   14 in older files, below

The last two had no marker of their own: bufr2wrk.py gave reflectivity, ZDR and
velocity a dedicated byte, but for the rain rate and the sums a missing point
simply saturated the top of the run length table.  Where that landed depended on
the decoder - 221 (1431 mm/h) from bufr2wrk and 164 (93 mm/h) from debufr.exe,
which the pipeline used until it changed over between 13 and 15 July 2026.  It
is per era, not per radar: every port switched on the same days.

bufr2wrk.py now writes 255 for both, the way it already did for velocity, so
files written since have an unambiguous marker.  The viewer folds what older
files carry, but only where folding is free:

    221   rain rate, bufr2wrk     1431 mm/h, unreachable       folded onto 255
    14    sums, both decoders     from the wrapped tail        folded onto 255
    164   rain rate, debufr.exe   93..97 mm/h, a real band     NOT folded

Blanking 164 would discard genuine extreme rainfall in every file, including the
ones written since, to tidy up files that age out of the archive on their own.
Pre-July-2026 rain maps therefore show a ring of 93-97 mm/h outside the coverage
- the old decoder's doing, and not distinguishable from data in what it wrote.
Untreated, 221 read as a 1467 mm/h downpour over the whole out-of-range area, or
73 dBZ when the same file is shown as maximum reflectivity.

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
Vertical cross sections
================================================================================

Left click the map, then click a second point, and the program cuts a vertical
section along that line.  There are two of them, one per front end, and they
share nothing but the two clicks.

--------------------------------------------------------------------------------
imagegcc: vert.c, over the map
--------------------------------------------------------------------------------

The original - the drawing and the scheme are as they were, and what has been
touched since is memory safety: three arrays it wrote past the ends of, an
expand() call reading a square out of a buffer that is not one, and a row index
taken from the product header without a check against zero.  One behavioural
change came with them.  The column index existed twice, as an int and as an
unsigned char, and the two loops that walk the cut used different ones; past
column 255 the char wrapped, which both scrambled the far end of a long section
and made the cut look shorter than it was when choosing the horizontal
resolution.  A long cut now gets the finer grid the code was always trying to
give it - a 421 cell cut goes from 187 columns to 500.

It has nowhere to put the section but the map window, so it paints over the
map; a third click reloads the file to get the map back.  The
sampling is the nearest grid cell along a Bresenham line, the vertical raster is
a fixed four rows per kilometre, and the gaps between the constant altitude
levels are filled by ramping the palette bytes from the level below to the one
above.  That ramp is what makes an echo top drift: it runs to zero over the
whole gap, so a level with 40 dBZ under an empty one paints a smooth 40 to 0
gradient across a kilometre where there is nothing at all.

--------------------------------------------------------------------------------
cross=x1,y1,x2,y2: the same section, with no window
--------------------------------------------------------------------------------

    ./gen-bitmap time22:00 cross=40,40,60,60

Cuts between two product grid cells and writes cross.png.  Nothing is displayed:
the batch build already renders through GRX's memory driver, so vert() paints
into that surface and it is saved from there.  The batch surface is normally
only as wide as the legend column - the map goes to RamContext and the legend is
all that is ever drawn on the surface itself - so init_graph() widens it when
cross= is given.

It exists because vert.c could not be reached except by clicking twice on a map.
Nothing headless had run a line of it, and it held a read of tens of kB past a
malloc: expand() doubles a square mapsiz by mapsiz array, and a section is
vert_size wide by MaxVertKm*VertLines tall.  That faults on Windows, where the
heap keeps an unmapped page close by, and goes unnoticed on glibc, whose arenas
absorb it.  tests/sanitize-test.sh drives five cuts through it on every push.

--------------------------------------------------------------------------------
imageqt: crosssect.c, in a window of its own
--------------------------------------------------------------------------------

The Qt build leaves the map alone - it draws only the line that was cut, which
survives panning, zooming and the next file - and opens the section in a
separate window, with the product, the interpolation and a PNG export of its
own.  crosssect.c computes it; the window is CrossSectionWindow in qtmain.cpp.

The interpolation is three dimensional and works in physical values rather than
in palette bytes:

  - bilinear across the map grid, on every level at once;
  - linear in altitude between the two levels bracketing the sample, at the
    altitudes the passport gives (bufhead[0]/10), not at a fixed spacing;
  - reflectivity averaged as linear Z and ZDR as the power ratio it is the
    logarithm of, so 20 dBZ beside 50 dBZ is 47, not 35.  Velocity is averaged
    as it stands.

The scheme is FormRLSAir1::viewClipPlane() from uvknew, including the two rules
that stop a smooth picture from becoming a wrong one:

  - a bilinear cell whose four corners are not all readings falls back to the
    nearest corner, so an echo edge stays an edge instead of being blended into
    the empty grid around it;
  - between two levels, one of which has no reading, the sample takes the other
    level's value only while that level is the nearer of the two, and is empty
    beyond.  Nothing is extrapolated above the highest level or below the
    lowest either.

Sampling is one column per half grid cell - the resolution the map itself is
drawn at - and 50 m rows up to the highest level.  Turning "3D interpolation"
off falls back to the nearest level and the nearest cell, which is the useful
comparison: the coverage should not change, only the smoothness.  If it does,
one of the two rules above is not doing its job.

A sample is therefore a good deal wider than it is tall - typically 2 km across
by 50 m up, which the status line spells out.  That, and not the drawing, is
where the vertical banding in the picture comes from: across the cut there is
only ever the map grid to go on.

"Smooth shading" is a separate thing from the interpolation and is off by
default.  It only decides whether Qt blends the colours together as the plot is
scaled up to the window.  The palette is a band scale, so a blend of two bands
is a colour that appears in no legend row and stands for no reading - which is
exactly what one should not have to squint past when reading a value off the
picture.  Smoothing belongs on the values, before a colour is chosen, and that
is what the interpolation already does.

Under the plot is a hatched band: how high the lowest beam of the nearest radar
passes, over a 4/3 earth.  Nothing under it was ever scanned, which is not the
same as nothing being there - and a reading can appear inside it, because the
constant altitude products are pseudo-CAPPI and carry the lowest tilt outward.
Ports that sent nothing are skipped when looking for the nearest radar; vert.c
does not skip them, and their position is still (0,0), the map's corner.

The dashed lines across the plot are the altitudes the data actually sits at.
Everything between them is interpolated, and it is worth being able to see
which is which.

The values are on the same byte scales the cursor readout prints (see
format_reading() in showdata.c): a point read off the map and off the section
have to give one answer.  Colouring goes back through the byte, because that is
what the .pal files are indexed by - so cross_section_byte() is the inverse of
those scales, and it keeps off the two bytes that are not readings, or an
interpolated sample could land on "no data" and punch a hole in an echo.

================================================================================
GRX 2.4.9 patches (2026 port to modern Linux)
================================================================================

The program links GRX 2.4.9, which does not build on a current toolchain
untouched.  The six patches in grx-patches/ are what make it, and they are
the source of truth - not the working tree on any particular machine.

--------------------------------------------------------------------------------
Building GRX, from nothing
--------------------------------------------------------------------------------

    curl -fLO http://grx.gnu.de/download/grx249.tar.gz
    mkdir grx-build && tar xzf grx249.tar.gz -C grx-build --strip-components=1

    for p in grx-patches/*.patch; do patch -p1 -d grx-build < "$p"; done

    make -C grx-build -f makefile.x11 libs        # X11 driver, for unix
    GRX=grx-build ./make.sh

`patch -p1 -d <tree>` wants the directory that holds makedefs.grx.  There are
two layouts in circulation and they differ on where that is: the tarball above
puts it at the root once --strip-components=1 has eaten the leading directory,
while the DJGPP-style grx249s.zip puts it under contrib/grx249.  Whatever -d
you patch with is what GRX= has to point at afterwards.  make.sh defaults to
../grx249/contrib/grx249, which is the zip layout.

    ONLY EVER PATCH A FRESHLY UNPACKED TREE.

That is not advice about wasted effort, it is about silent damage.  Patch 03
changes one line of vd_mem.c, and the four lines of context around it are
character for character identical to another struct twenty lines above -
gr8ext, the 8bpp mode, whose colour component positions are legitimately
{ 0, 0, 0 }.  Once gr24ext has been patched, the intended site no longer
matches, patch finds the other one, reports a cheerful "Hunk #1 succeeded at
90 (offset -19 lines)" and corrupts the palette mode instead.  -N does not
protect you: this is not a reversed application, it is a different one that
looks perfectly valid.  When in doubt, delete the tree and unpack it again.

Rebuilding after patching has to be FROM CLEAN, because the affected types
and tables are baked into every object file:

    make -C grx-build -f makefile.x11 clean
    make -C grx-build -f makefile.x11 libs

The five workflows under .github/workflows/ do all of this on every push, and
are the other place to read the procedure - linux.yml is the shortest of them.
They fetch the tarball by checksum, so they also record which upstream tarball
the patches are cut against.

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
4. src/include/memfill.h - sttzero clears the whole struct       needed on win64
--------------------------------------------------------------------------------

    -#define sttzero(p)              memzero((p),sizeof(*(p)))
    +#define sttzero(p)              memset((p),0,sizeof(*(p)))

memzero() scales the byte count for a wider unit than the fill actually
writes, so on some builds it clears only part of the object.

    Symptom: on Win64 it cleared 52 bytes of a 104 byte GrContext, leaving
    gc_xoffset and gc_yoffset as heap garbage that was then added to every
    draw coordinate.  Linux hides it completely - fresh heap pages are zero
    there, so the half that never gets cleared is already zero.

That is why an unpatched tree passes tests/sttzero.c on Linux and fails it on
Windows.  A Linux-only build survives without this patch; do not read that as
not needing it.

--------------------------------------------------------------------------------
5. src/include/access24.h - endianness test                     needed on mingw
--------------------------------------------------------------------------------

    -#if BYTE_ORDER==LITTLE_ENDIAN
    +#if !(defined(BYTE_ORDER) && defined(BIG_ENDIAN) && BYTE_ORDER==BIG_ENDIAN)

An undefined BYTE_ORDER evaluates to 0 in #if, so a platform that defines
LITTLE_ENDIAN but not BYTE_ORDER - mingw - silently took the big endian
branch and read the colour bytes off by one.  Testing for known big endian
instead means an unknown platform gets the little endian variant, which is
the one that is right nearly everywhere.

--------------------------------------------------------------------------------
6. five files - macOS is a unix                                 needed on darwin
--------------------------------------------------------------------------------

    include/grx20.h, src/mouse/input.h, src/vdrivers/vd_xwin.c,
    src/utilprog/bin2c.c, src/bgi/bccgrx00.h

darwin defines neither `unix` nor `__unix__`, so GRX fell through to its
"#error unknown platform" and, with GRX_VERSION never set, every later `far`
went undeclared behind it.  The patch adds __APPLE__ && __MACH__ everywhere
that list is tested, gives darwin a timer, and renames a static in vd_xwin.c
that collides with a libc symbol there.

It touches nothing on Linux or Windows, so it is applied unconditionally
rather than being kept for macOS builds only.

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

Neither front end needs a real screen to be checked: Xephyr gives a nested
one, and imageqt must be told to use it, because the Qt plugin otherwise
picks Wayland and ignores DISPLAY entirely.

    Xephyr :77 -screen 1920x1080x24 &
    DISPLAY=:77 ./imagegcc
    DISPLAY=:77 ./imageqt -platform xcb
    DISPLAY=:77 import -window root shot.png

================================================================================
GeoTIFF output
================================================================================

gen-bitmap normally saves a picture - the map as drawn, geography and legend
on top, in screen pixels.  `geotiff` writes the mosaic itself instead: one
byte per grid cell, the product's palette as the TIFF colour map, and the
georeferencing GDAL needs to place it.  It is the same product
radar-wms/geotiff.py builds through pycao, without going through Python.

    ./gen-bitmap map1  geotiff=refl.tif      # reflectivity level 1
    ./gen-bitmap zdr3  geotiff=zdr3.tif
    ./gen-bitmap rain  time10:00 geotiff=rain.tif
    ./gen-bitmap sum1  geotiff=q1.tif

No window is opened and nothing is drawn, so the geography overlay is not on
it by construction.  The product must be present in the file: if it is not,
nothing is written and the exit status is 1, rather than the on screen
behaviour of falling back to whatever the file does carry.

The grid goes out in the projection it is already built in - equidistant
conic, centred on the CenterX/CenterY of image.cfg - so nothing is resampled
and no value changes:

    Size is 1500, 1500
    Origin = (-3000000.000, 3000000.000)      Pixel Size = (4000.0, -4000.0)
    Center ( 0.0, 0.0 ) ( 50d 0' 0.00"E, 54d 0' 0.01"N)
    NoData Value=254

That is not the frame pycao uses, which is `+proj=sterea +lat_0=50 +lon_0=100`.
Reproject if a consumer wants that one:

    gdalwarp -t_srs '+proj=sterea +lat_0=50 +lon_0=100' -r near in.tif out.tif

`-r near` matters: the band holds palette indices, not intensities, so an
interpolating resampler would average byte 3 and byte 9 into a value that
means something else entirely.

================================================================================
GrBitBlt and frame modes
================================================================================

The map is drawn into a RAM context and copied to the window in one go.  Two
things about GrBitBlt() are worth knowing before touching that code.

GrBitBltNC() picks its transfer routine from the frame modes of the two
contexts, and its if/else chain ends in a bare `else return;` - so a pair it
has no routine for is not an error, it simply draws nothing.  A source is
usable only when its frame mode is the one the screen driver names as its
compatible RAM mode.  That is *not* GR_frameRAM24 on a normal X server:

    X visual depth 24  ->  GR_frameXWIN32L, compatible RAM mode RAM32L
    X visual depth 16  ->  GR_frameXWIN16,  compatible RAM mode RAM16

so a hardcoded RAM24 source silently copies nothing on both.  imagegcc used
to work around that by writing the map to /tmp/output.png with
GrSaveContextToPng() and reading it back with GrLoadContextFromPng() - 130 ms
per frame at 1500x1100, against 6 ms for the blit.  Ask the driver instead:

    RamContext = GrCreateFrameContext(GrCoreFrameMode(), ...);

GrCoreFrameMode() is the screen driver's rmode, so the pair always matches.
Note that it is meaningful only under a real video driver - the memory driver
leaves rmode at GR_frameUndef, which is why imageqt and gen-bitmap still name
GR_frameRAM24 explicitly.  They never blit to a screen; they read the surface
back directly.

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

All of grx-patches/ applies here too, and patches 4 and 5 matter more on this
platform than anywhere else - see their sections above; both are Windows bugs
that Linux hides.  Patch 2 (libgrx.h) is not
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

  * GRX 2.4.9 builds for Windows from the upstream tarball with grx-patches/
    applied - lib/win32/libgrx20.a, no errors
  * every .c file of this program compiles for Windows, with no implicit
    declarations and no missing headers
  * unsigned __int128, which the port bitmask needs, compiles and links

Not yet verified: linking against the Windows builds of zlib, libpng, PROJ,
minizip and Qt5.  That is what the CI run exercises.

The patches are generated against the upstream grx249.tar.gz above.  The
DJGPP-style grx249s.zip distribution has some makedefs.grx defaults set
differently; applying these patches to it reports one hunk as already
applied, which is harmless (the workflow passes -N for that reason).

================================================================================
Tests and sanitizers
================================================================================

    cd tests && ./run-tests.sh       GRX probes, see tests/README
    ./tests/geotiff-test.sh          render the fixture, read every TIFF back
    ./tests/sanitize-test.sh         the same fixture under ASan and UBSan

All three take GRX=<grx-tree> from the environment.  The fixture is tests/data,
built from the BUFR messages in tests/bufr.

sanitize-test.sh rebuilds with SAN=1, which adds -fsanitize=address,undefined
and -fno-sanitize-recover=all - undefined behaviour that only prints is
undefined behaviour nobody reads - and then puts the fixture through five
renders and five cuts.  The SAN build stops after gen-bitmap and imagegcc:
libimage.so refuses undefined symbols on purpose, and the sanitizer runtime is
exactly that in a shared object.  It leaves sanitized binaries behind, so
rebuild with plain ./make.sh if you meant to keep the real ones.

Leak detection is off.  The program frees very little and never meant to - it
renders and exits - and leak reports would bury the memory errors this is for.

linux.yml runs it on x86_64 and arm64, after the artifact upload so that the
binaries which ship are not the sanitized ones.  Two things it caught that
years of CI had not:

  * the expand() overread described under cross= above, as a heap-buffer-
    overflow 0 bytes past a 4964 byte region allocated in vert.c;

  * void main.  Outside the Qt build the function in image.c is main, and it
    was declared void, so a render that returned normally instead of through
    one of the exit(0) paths left the exit status to whatever happened to be
    in the return register.  x86-64 held 0 there and thirty years passed;
    aarch64 held 80.

UBSan also reported every read and write of a short in the packed coordinate
tables of coord.c: a byte count sits in front of each one, so all of them are
at odd addresses.  They go through get_short()/put_short() now, which memcpy
and compile to the same instruction.  Keep the run clean - a report that is
known and ignored hides the next one that is not.

================================================================================
Releases
================================================================================

    git tag v1.0 && git push origin v1.0

.github/workflows/release.yml runs the three platform builds and attaches their
bundles to a GitHub release for the tag:

    imagegcc-windows-x64.zip       mingw64 under MSYS2
    imagegcc-windows-arm64.zip     CLANGARM64 under MSYS2
    imagegcc-macos-ARM64.tar.gz    imageqt.app and the command line tools
    HOW-TO-RUN-macos.txt

The builds are macos.yml, windows.yml and windows-arm64.yml, called rather than
copied - a release has to ship what CI has been testing, not a second recipe
that drifts from it.  That is also why those three carry branches: '**' on
their push trigger: without it a tag would build each of them twice, once
directly and once through release.yml.

macOS ships as a tarball and has to.  GitHub's artifact zip drops the
executable bits and dereferences the symlinks a Qt .framework is made of, which
breaks the ad hoc signature the bundle is loaded with - and on Apple silicon
that is not a warning, the binary is killed on load with nothing useful said.
Unpacking still needs

    xattr -dr com.apple.quarantine dist

because the bundle is ad hoc signed and not notarized; HOW-TO-RUN-macos.txt
ships alongside it saying so.  Windows has neither problem and gets zips.

Linux is deliberately not in the release: a glibc build is only good on the
distribution that made it, so there is nothing useful to attach.  Build from
source there.

Re-running the publish job for a tag that already has a release replaces the
files rather than failing on the create.
