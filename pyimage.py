#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Python bindings for the IMAGE radar compositor.

The C code that gen-bitmap uses to read the archive, mosaic the radars and
write a GeoTIFF is also built as libimage.so, and this drives it in process
through ctypes - no subprocess per frame.  It is the same work
radar-wms/generate_bufr_archive_test.py does through pycao, from the C engine
instead:

    import pyimage, datetime

    with pyimage.Archive("paths") as ar:
        frame = ar.nearest(datetime.datetime(2026, 7, 28, 0, 10))
        frame.load("dbz1")

        for r in frame.radars:                     # feature motion vectors
            if r.moving:
                update_vector.update(frame.timestamp, r.name,
                                     r.speed_kmh / 3.6, r.azimuth_deg,
                                     r.lat, r.lon)

        info = frame.geotiff("out.tif")            # writes, returns geometry
        update(frame.timestamp, info["proj4"], info["bbox"], "bufr_dbz1")

libimage.so carries no graphics driver: GRX ships as a non-PIC static library
and cannot be linked into a shared object, so the library is built from the
sources that do not draw (see make.sh).  Everything here is therefore data -
there is no window, no palette allocation and no legend.

The caveat worth knowing: the C engine keeps its state in globals, so an
Archive is not reentrant - one at a time, in one thread, and the working
directory is the process's own.  A long-lived server has to give the engine a
process to itself; radar-wms/image_engine.py is what that looks like under
mod_wsgi.

Serving requests from it used to have a second caveat, that the engine
answered a problem by calling exit() and took the host process with it.  On
the per-request path that is gone: get_ptr() returns NULL for a radar whose
coordinate table will not build and read_files() leaves that radar out of the
mosaic.  What remains is start-up only - a missing or malformed
config/image.cfg, and a first palette that will not load (palette.c keeps a
working one after that).  Open the Archive once, at start-up, where an exit is
a failure to start rather than a request that killed the worker.
"""

import ctypes
import datetime
import json
import os
import struct
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

# maps[] is built by init_maps(); these are the map_type values from image.h,
# which is what map_index() takes.
MAP_0, MAP_H, MAP_S, MAP_P = 0, 16, 17, 18
MAP_Q1, MAP_D1, MAP_V1 = 19, 24, 34

#: product name -> map_type.  The names match gen-bitmap's arguments.
PRODUCTS = {"rain": MAP_P, "top": MAP_H, "phenom": MAP_S}
for _i in range(16):
    PRODUCTS["dbz%d" % _i] = MAP_0 + _i
for _i in range(1, 11):
    PRODUCTS["zdr%d" % _i] = MAP_D1 + _i - 1
    PRODUCTS["vel%d" % _i] = MAP_V1 + _i - 1
for _i, _h in enumerate((1, 3, 6, 12, 24)):
    PRODUCTS["sum%d" % _h] = MAP_Q1 + _i

#: product family, as image.h numbers them.  A cross section is cut through a
#: family - all the levels of it the frame carries - not through one product.
FAM_DBZ, FAM_ZDR, FAM_VEL = 0, 3, 4
FAMILIES = {"dbz": FAM_DBZ, "zdr": FAM_ZDR, "vel": FAM_VEL}

#: MAP_ALL from image.h - every product, which is what want_maps starts as.
MAP_ALL = (1 << 64) - 1


class ImageError(Exception):
    pass


def _cp866(raw):
    """A label from the engine.  They are cp866 - the encoding the archive,
    the config files and the .wrk headers have always been in."""
    if raw is None:
        return ""
    return raw.decode("cp866", errors="replace").strip()


class _MyFile(ctypes.Structure):
    """struct MyFile in image.h - one archive frame.

    `flag` is an unsigned __int128 port mask, which ctypes has no type for.
    Two 64 bit halves hold the same bits, but the alignment does not follow:
    __int128 aligns to 16, so the five date bytes are followed by eleven of
    padding and the whole struct is 32 bytes.  Left to itself ctypes would
    align the halves to 8, make the struct 24, and read every frame after the
    first from the wrong offset.
    """
    _fields_ = [("year", ctypes.c_ubyte), ("month", ctypes.c_ubyte),
                ("day", ctypes.c_ubyte), ("hour", ctypes.c_ubyte),
                ("minute", ctypes.c_ubyte),
                ("_pad", ctypes.c_ubyte * 11),
                ("flag_lo", ctypes.c_uint64), ("flag_hi", ctypes.c_uint64)]

    @property
    def ports(self):
        mask = (self.flag_hi << 64) | self.flag_lo
        return [p + 1 for p in range(128) if mask >> p & 1]


class Radar(object):
    """One radar's report for a frame, as header.wrk carried it."""

    __slots__ = ("port", "name", "lon", "lat", "speed_kmh", "azimuth_deg")

    def __init__(self, d):
        self.port = d["port"]
        self.name = d["name"].strip()
        self.lon = d["lon"]
        self.lat = d["lat"]
        self.speed_kmh = d["speed_kmh"]
        self.azimuth_deg = d["azimuth_deg"]

    @property
    def moving(self):
        """False when the radar reported no feature motion.

        bufr2wrk.py normalises a missing vector to speed 0 with azimuth 511,
        which is the same test the WMS side makes before storing one.
        """
        return self.speed_kmh > 0 and self.azimuth_deg != 511

    def __repr__(self):
        return ("<Radar port=%d %s %.3fE %.3fN %.0f km/h bearing %d>"
                % (self.port, self.name, self.lon, self.lat,
                   self.speed_kmh, self.azimuth_deg))


class CrossSection(object):
    """A vertical slice through the levels of one family.

    `data` is width*height palette bytes, row 0 at the TOP, to be looked up in
    `palette` - the same 256 colours the map and the GeoTIFF use, so a section
    and a map of the same frame cannot disagree about what a colour means.
    Cells the beam never reached carry `nodata`, which png() makes transparent.
    """

    #: what `values` carries where the beam never reached.  CS_NO_VALUE in
    #: qt_bridge.h - the two have to agree.
    NO_VALUE = -9999.0

    def __init__(self, width, height, data, palette, nodata,
                 length_km, top_km, base_km, family,
                 values=None, floor_km=None, units="", title=""):
        self.width = width
        self.height = height
        self.data = data
        self.palette = palette          # 768 bytes, RGB
        self.nodata = nodata
        self.length_km = length_km      # what width spans
        self.top_km = top_km            # what height spans
        self.base_km = base_km          # the lowest level; below it is guesswork
        self.family = family
        #: physical values, width*height of them, row 0 at the TOP like
        #: `data`, or None when the section was not asked for them.  Empty
        #: cells carry NO_VALUE.  dBZ, dB or m/s - `units` says which.
        self.values = values
        #: width altitudes: the lowest the beam reaches at each step along the
        #: line.  Under it the section is empty because nothing looked.
        self.floor_km = floor_km
        self.units = units              # cp866, from the palette
        self.title = title              # cp866, the product's own name

    def png(self, path):
        """Write it as a palette PNG, no-data transparent.  Standard library
        only: zlib is what a PNG is, and pulling in an imaging package to
        write 40 kB of pixels would be the largest dependency in the tree."""
        def chunk(tag, payload):
            return (struct.pack(">I", len(payload)) + tag + payload +
                    struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

        alpha = bytearray(b"\xff" * 256)
        if 0 <= self.nodata < 256:
            alpha[self.nodata] = 0
        raw = bytearray()
        for row in range(self.height):
            raw.append(0)               # filter type 0, none
            raw += self.data[row * self.width:(row + 1) * self.width]

        with open(path, "wb") as handle:
            handle.write(b"\x89PNG\r\n\x1a\n")
            handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", self.width,
                                                    self.height, 8, 3, 0, 0, 0)))
            handle.write(chunk(b"PLTE", bytes(self.palette)))
            handle.write(chunk(b"tRNS", bytes(alpha)))
            handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
            handle.write(chunk(b"IEND", b""))
        return path

    def array(self):
        """The bytes as a numpy (height, width) uint8 array.  Needs numpy."""
        import numpy
        return numpy.frombuffer(self.data, numpy.uint8).reshape(self.height,
                                                                self.width)

    def value_rows(self, empty=None):
        """`values` as a list of rows, top row first, for serialising.

        Cells the beam never reached come out as `empty` - None by default,
        which is what JSON has and what a plotting library reads as a gap.
        Raises when the section was cut without values=True; there is nothing
        sensible to return, and quietly handing back the palette bytes would
        put indices where a caller expects dBZ.
        """
        if self.values is None:
            raise ImageError("this section carries no values - cut it with "
                             "cross_section(..., values=True)")
        rows = []
        for row in range(self.height):
            start = row * self.width
            rows.append([empty if v <= self.NO_VALUE else round(v, 2)
                         for v in self.values[start:start + self.width]])
        return rows

    def __repr__(self):
        return ("<CrossSection %dx%d cells, %.0f km along, %.1f..%.1f km up>"
                % (self.width, self.height, self.length_km, self.base_km,
                   self.top_km))


class Frame(object):
    """One observation time.  `load()` builds the mosaic for a product."""

    def __init__(self, archive, index, stamp, ports):
        self._archive = archive
        self.index = index
        self.timestamp = stamp
        self.ports = ports
        self.product = None
        self._info = None

    def load(self, product, only=None):
        """Read every radar for this frame and mosaic one product.

        `only` is a family name - "dbz", "zdr", "vel" - and narrows the read
        to that family's levels plus the product asked for.  A vertical
        section needs no more than that, and the default (everything) costs
        about three times as much.  Leave it alone for a GeoTIFF or a grid,
        where the other products are wanted afterwards.
        """
        self._archive._load(self, product, only)
        return self

    @property
    def info(self):
        """Grid geometry, projection and the radar vectors."""
        if self._info is None:
            self._info = self._archive._info()
        return self._info

    @property
    def radars(self):
        return [Radar(r) for r in self.info["radars"]]

    def geotiff(self, path):
        """Write the mosaic as a GeoTIFF; returns the same dict as `info`."""
        self._archive._geotiff(path)
        return self.info

    def grid(self):
        """The mosaic as raw .wrk bytes: size*size of them, row 0 at the
        north edge, the same grid and the same byte values geotiff() writes.

        A byte is a byte: it means whatever the product's palette says it
        means, and for differential reflectivity that scale wraps - see
        zdr_value() in palette.c.  `info['nodata']` names the empty byte.
        """
        return self._archive._grid()

    def cross_section(self, x1, y1, x2, y2, family="zdr", smooth=True,
                      values=False):
        """Cut a vertical section between two cells of the product grid.

        The endpoints are grid cells, the same ones grid() is indexed by;
        `info` carries bbox, pixel_m and proj4 for turning a longitude and a
        latitude into a pair.  `family` is "zdr", "dbz" or "vel" - a section
        needs at least two levels of it, and returns None when the frame does
        not carry that many.  Any product of the frame has to be load()ed
        first; which one does not matter, the levels are found from the family.

        `values` cuts the line a second time for the physical numbers, in the
        units the engine reports; without it the section is palette bytes
        only, which is all a picture needs.
        """
        return self._archive._cross_section(x1, y1, x2, y2, family, smooth,
                                            values)

    def families(self):
        """The families this frame can be cut through - see Archive.families."""
        return self._archive.families()

    def levels(self, family):
        """How many levels of `family` this frame carries."""
        return self._archive.levels(family)

    def legend(self, family):
        """The family's legend rows - see Archive.legend."""
        return self._archive.legend(family)

    def array(self):
        """grid() as a numpy (size, size) uint8 array.  Needs numpy."""
        import numpy
        data = self.grid()
        side = int(round(len(data) ** 0.5))
        return numpy.frombuffer(data, numpy.uint8).reshape(side, side)

    def __repr__(self):
        return "<Frame %s ports=%d product=%s>" % (
            self.timestamp.strftime("%Y-%m-%d %H:%M"),
            len(self.ports), self.product)


class Archive(object):
    """The archive named by a `paths` file.

    Not reentrant: the C engine keeps one composite in globals, so one Archive
    at a time and never across threads.
    """

    _open = None

    #: the want_maps bits the last _load() actually read, so a caller can tell
    #: whether what it needs is already in the composite
    loaded_mask = 0

    def __init__(self, paths="paths", cfg="image.cfg", library=None,
                 workdir=None):
        """`paths` names the path file; CFG/GRF/MAP inside it are resolved
        relative to `workdir`, which defaults to the directory holding it.

        The C engine reads those relative to the process working directory, so
        the Archive chdirs there for its lifetime and restores on close().  Any
        relative path a caller passes to geotiff() would resolve against it -
        pass absolute ones.
        """
        if Archive._open is not None:
            raise ImageError("an Archive is already open - the C engine keeps "
                             "its state in globals and cannot hold two")

        paths = os.path.abspath(paths)
        workdir = workdir or os.path.dirname(paths)
        # os.chdir would raise a bare FileNotFoundError naming only the
        # directory, which is a puzzle when the caller never mentioned one
        if not os.path.isfile(paths):
            raise ImageError("no path file at %s - pass paths=..., or set "
                             "IMAGE_HOME to the imagegcc checkout" % paths)
        if not os.path.isdir(workdir):
            raise ImageError("%s does not exist; it is where %s lives and "
                             "where CFG/GRF/MAP inside it are resolved from"
                             % (workdir, os.path.basename(paths)))
        self._cwd = os.getcwd()
        os.chdir(workdir)

        # from here on the process is somewhere else; put it back if any of
        # this throws, or the caller is left in a directory it never chose
        try:

            self._lib = self._load_library(library)
            self._lib.image_api_init()

            # read_cfg() builds maps[] and reads image.cfg; read_dir() scans the
            # archive; init_files() allocates the passports.  This is the order
            # main() uses, and none of it may be skipped.
            self._lib.read_cfg(paths.encode(), cfg.encode())
            self._lib.read_dir()
            self._lib.init_files()

            count = ctypes.c_int.in_dll(self._lib, "FilesRead").value
            if count <= 0:
                raise ImageError("no frames found - check the MAP line in %s" % paths)
            files = (_MyFile * count).in_dll(self._lib, "Files")

            self.frames = []
            for i in range(count):
                f = files[i]
                try:
                    stamp = datetime.datetime(2000 + f.year, f.month, f.day,
                                              f.hour, f.minute)
                except ValueError:      # a corrupt name in the archive directory
                    continue
                self.frames.append(Frame(self, i, stamp, f.ports))
        except Exception:
            os.chdir(self._cwd)
            raise

        Archive._open = self

    @staticmethod
    def _load_library(path):
        if path is None:
            # make.sh names it libimage.dll on windows and libimage.so
            # everywhere else, macOS included - dlopen does not mind the
            # suffix and this saves the caller knowing which platform it is on
            for name in ("libimage.so", "libimage.dll", "libimage.dylib"):
                path = os.path.join(HERE, name)
                if os.path.exists(path):
                    break
        if not os.path.exists(path):
            raise ImageError("no libimage library in %s - run ./make.sh" % HERE)
        lib = ctypes.CDLL(path)
        lib.read_cfg.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        lib.read_files.argtypes = [ctypes.c_int, ctypes.c_int]
        lib.set_cur_map.argtypes = [ctypes.c_int]
        lib.map_index.argtypes = [ctypes.c_int]
        lib.map_index.restype = ctypes.c_int
        lib.cross_section_raster.argtypes = [
            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
            ctypes.POINTER(ctypes.c_float)]
        lib.cross_section_raster.restype = ctypes.c_int
        lib.cross_section_values.argtypes = [
            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_float),
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
            ctypes.POINTER(ctypes.c_float),
            ctypes.POINTER(ctypes.c_float), ctypes.c_int]
        lib.cross_section_values.restype = ctypes.c_int
        lib.cross_section_colors.argtypes = [ctypes.c_int, ctypes.c_char_p]
        lib.cross_section_colors.restype = ctypes.c_int
        lib.cross_section_nodata.argtypes = [ctypes.c_int]
        lib.cross_section_nodata.restype = ctypes.c_int
        lib.cross_section_legend.argtypes = [ctypes.c_int, ctypes.c_int,
                                             ctypes.c_char_p,
                                             ctypes.c_char_p, ctypes.c_int]
        lib.cross_section_legend.restype = ctypes.c_int
        lib.cross_section_levels.argtypes = [ctypes.c_int]
        lib.cross_section_levels.restype = ctypes.c_int
        lib.cross_section_families.argtypes = [ctypes.POINTER(ctypes.c_int),
                                               ctypes.c_int]
        lib.cross_section_families.restype = ctypes.c_int
        lib.cross_section_units.argtypes = [ctypes.c_int]
        lib.cross_section_units.restype = ctypes.c_char_p
        lib.cross_section_title.argtypes = [ctypes.c_int]
        lib.cross_section_title.restype = ctypes.c_char_p
        lib.map_grid.argtypes = [ctypes.c_int, ctypes.c_char_p]
        lib.map_grid.restype = ctypes.c_int
        lib.save_geotiff.argtypes = [ctypes.c_char_p]
        lib.save_geotiff.restype = ctypes.c_int
        lib.save_info.argtypes = [ctypes.c_char_p]
        lib.save_info.restype = ctypes.c_int
        lib.product_count.restype = ctypes.c_int
        lib.product_family_of.argtypes = [ctypes.c_int]
        lib.product_family_of.restype = ctypes.c_int
        lib.product_level_of.argtypes = [ctypes.c_int]
        lib.product_level_of.restype = ctypes.c_int
        return lib

    # -- which products a read should bother with -------------------------

    def family_mask(self, family):
        """The want_maps bits for every product of one family.

        read_files() unzips and mosaics whatever is selected, and doing all
        forty-four products when a section needs the eleven of one family
        costs about three times as much.  See want_maps in files.c.
        """
        fam = FAMILIES[family] if isinstance(family, str) else int(family)
        mask = 0
        for i in range(self._lib.product_count()):
            if self._lib.product_family_of(i) == fam:
                mask |= 1 << i
        return mask

    @property
    def all_mask(self):
        """Every product - what the viewer wants, and the default."""
        return (1 << self._lib.product_count()) - 1

    def _set_want(self, mask):
        ctypes.c_ulonglong.in_dll(self._lib, "want_maps").value = mask

    def nearest(self, when):
        """The frame closest to `when`."""
        if not self.frames:
            raise ImageError("the archive is empty")
        return min(self.frames, key=lambda f: abs(f.timestamp - when))

    def between(self, start, end):
        return [f for f in self.frames if start <= f.timestamp < end]

    # -- the parts that touch the engine's globals ------------------------

    def _load(self, frame, product, only=None):
        try:
            want = PRODUCTS[product]
        except KeyError:
            raise ImageError("unknown product %r - one of %s"
                             % (product, ", ".join(sorted(PRODUCTS))))

        # `only` narrows what gets unzipped and mosaicked to one family.  The
        # default reads everything, which is what the viewer wants and what
        # makes the other products available without another pass; a caller
        # after a section through one family pays about a third as much by
        # saying so.  The product being selected has to be in the set, or
        # set_cur_map() would land on something that was never loaded.
        mask = self.all_mask if only is None else \
            self.family_mask(only) | (1 << self._lib.map_index(want))
        self._set_want(mask)
        try:
            # flag 0 loads every product the mask allows, which is what makes
            # the radar vectors and the other levels available afterwards
            self._lib.read_files(frame.index, 0)
        finally:
            # back to MAP_ALL, the C default, rather than to a mask computed
            # from no_maps: the global belongs to everything else in the
            # process and should be found exactly as it started.
            self._set_want(MAP_ALL)
        self._lib.set_cur_map(want)
        self.loaded_mask = mask

        # set_cur_map() falls back to any product the frame does carry, which
        # is right on a screen and wrong here - the caller asked for one.
        current = ctypes.c_int.in_dll(self._lib, "current_map").value
        if current != self._lib.map_index(want):
            raise ImageError("frame %s carries no %s"
                             % (frame.timestamp.strftime("%Y-%m-%d %H:%M"),
                                product))
        frame.product = product
        frame._info = None
        self._current = frame

    def _info(self):
        fd, path = tempfile.mkstemp(suffix=".json")
        os.close(fd)
        try:
            if not self._lib.save_info(path.encode()):
                raise ImageError("could not read the frame metadata")
            with open(path, encoding="utf-8") as handle:
                return json.load(handle)
        finally:
            os.unlink(path)

    def _grid(self):
        # map_grid() copies the composite out whole.  Reading it cell by
        # cell through map_value() is the obvious thing to try and is
        # wrong: those coordinates are the doubled display ones, so a walk
        # over size*size cells covers a quarter of the map.
        size = self._current.info["size"] if self._current else 0
        if not size:
            raise ImageError("no product loaded - call Frame.load() first")
        buf = ctypes.create_string_buffer(size * size)
        want = PRODUCTS[self._current.product]
        side = self._lib.map_grid(want, buf)
        if side != size:
            raise ImageError("map_grid returned a %d cell grid, expected %d"
                             % (side, size))
        return buf.raw

    def _cross_section(self, x1, y1, x2, y2, family, smooth, values):
        try:
            fam = FAMILIES[family] if isinstance(family, str) else int(family)
        except KeyError:
            raise ImageError("unknown family %r - one of %s"
                             % (family, ", ".join(sorted(FAMILIES))))
        if self._current is None:
            raise ImageError("no frame loaded - call Frame.load() first")

        w, h = ctypes.c_int(), ctypes.c_int()
        length, top, base = (ctypes.c_float(), ctypes.c_float(), ctypes.c_float())
        args = [x1, y1, x2, y2, fam, 1 if smooth else 0]
        # the sizing call first: how many levels there are, and how long the
        # line is, decide the raster
        if not self._lib.cross_section_raster(*(args + [None, 0,
                ctypes.byref(w), ctypes.byref(h), ctypes.byref(length),
                ctypes.byref(top), ctypes.byref(base)])):
            return None
        cells = w.value * h.value
        buf = ctypes.create_string_buffer(cells)
        if not self._lib.cross_section_raster(*(args + [buf, cells,
                ctypes.byref(w), ctypes.byref(h), ctypes.byref(length),
                ctypes.byref(top), ctypes.byref(base)])):
            return None

        rgb = ctypes.create_string_buffer(256 * 3)
        if not self._lib.cross_section_colors(fam, rgb):
            raise ImageError("no palette for family %r" % family)

        # The values come from a second cut of the same line rather than from
        # the bytes above: the engine quantises on the way out, and the whole
        # point of asking for them is to get what it quantised.  Same
        # arguments, so the two rasters are the same grid.
        vals = floor = None
        if values:
            vbuf = (ctypes.c_float * cells)()
            fbuf = (ctypes.c_float * w.value)()
            if not self._lib.cross_section_values(*(args + [vbuf, cells,
                    ctypes.byref(w), ctypes.byref(h), ctypes.byref(length),
                    ctypes.byref(top), ctypes.byref(base), fbuf, w.value])):
                return None
            vals, floor = list(vbuf), list(fbuf)

        # the family's empty byte, not the loaded product's: they differ, and
        # reading it off the product paints the empty half of the picture
        nodata = self._lib.cross_section_nodata(fam)
        return CrossSection(w.value, h.value, buf.raw, rgb.raw, nodata,
                            length.value, top.value, base.value, fam,
                            vals, floor,
                            _cp866(self._lib.cross_section_units(fam)),
                            _cp866(self._lib.cross_section_title(fam)))

    def families(self):
        """The families this frame can be cut through, most useful first.

        A section needs two levels to interpolate between, so a frame that
        carries one differential reflectivity level offers no "zdr".  Names,
        the ones cross_section() takes.
        """
        buf = (ctypes.c_int * len(FAMILIES))()
        n = self._lib.cross_section_families(buf, len(FAMILIES))
        byvalue = {v: k for k, v in FAMILIES.items()}
        return [byvalue[buf[i]] for i in range(n) if buf[i] in byvalue]

    def levels(self, family):
        """How many levels of a family the loaded frame carries."""
        fam = FAMILIES[family] if isinstance(family, str) else int(family)
        return self._lib.cross_section_levels(fam)

    def legend(self, family):
        """The family's legend, strongest first: [(label, (r, g, b)), ...].

        The rows the viewer draws down the side of its window.  A web page
        that builds its own colour scale out of a value range will not match
        the map, however carefully it is chosen; this is the map's own, so it
        does.  Labels are cp866 in the palette files and text here.
        """
        fam = FAMILIES[family] if isinstance(family, str) else int(family)
        rows, row = [], 0
        while True:
            rgb = ctypes.create_string_buffer(3)
            label = ctypes.create_string_buffer(64)
            if not self._lib.cross_section_legend(fam, row, rgb, label, 64):
                return rows
            rows.append((_cp866(label.value), tuple(rgb.raw)))
            row += 1

    def _geotiff(self, path):
        path = os.path.abspath(path)          # the engine runs in workdir
        if not self._lib.save_geotiff(str(path).encode()):
            raise ImageError("could not write %s" % path)

    # -- lifetime ---------------------------------------------------------

    def close(self):
        if Archive._open is self:
            self._lib.de_init_files()
            os.chdir(self._cwd)
            Archive._open = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
