#!/usr/bin/env python3
"""Check a GeoTIFF written by `gen-bitmap ... geotiff=<file>`.

gdalinfo would say all of this and more, but no CI runner here has GDAL, and
the rest of tests/ deliberately depends on nothing outside the standard
library.  A TIFF directory is simple enough to read directly, and zlib - which
is in the standard library - unpacks the strips, so the pixels can be checked
too rather than just the header.

    python3 tests/geotiff-check.py FILE [--size N] [--nodata N]
                                        [--pixel M] [--origin X,Y]
                                        [--min-data N]

Prints one line per property checked and exits non-zero on the first failure.
"""

import struct
import sys
import zlib
import argparse
import zipfile

# the tags this writer sets, by number
IMAGEWIDTH, IMAGELENGTH, BITSPERSAMPLE = 256, 257, 258
COMPRESSION, PHOTOMETRIC = 259, 262
STRIPOFFSETS, SAMPLESPERPIXEL, ROWSPERSTRIP, STRIPBYTECOUNTS = 273, 277, 278, 279
COLORMAP = 320
GEOPIXELSCALE, GEOTIEPOINTS = 33550, 33922
GEOKEYDIRECTORY, GEODOUBLEPARAMS, GEOASCIIPARAMS = 34735, 34736, 34737
GDAL_NODATA = 42113

TYPESIZE = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 6: 1, 7: 1, 8: 2, 9: 4, 10: 8,
            11: 4, 12: 8}
TYPEFMT = {1: "B", 2: "c", 3: "H", 4: "I", 6: "b", 8: "h", 9: "i",
           11: "f", 12: "d"}


class TiffError(Exception):
    pass


def read_ifd(blob):
    """Tag number -> list of values, for the first directory."""
    if blob[:2] == b"II":
        end = "<"
    elif blob[:2] == b"MM":
        end = ">"
    else:
        raise TiffError("not a TIFF: byte order is %r" % blob[:2])
    magic, offset = struct.unpack(end + "HI", blob[2:8])
    if magic != 42:
        raise TiffError("not a classic TIFF (magic %d)" % magic)

    count = struct.unpack(end + "H", blob[offset:offset + 2])[0]
    tags = {}
    for i in range(count):
        at = offset + 2 + i * 12
        tag, typ, n = struct.unpack(end + "HHI", blob[at:at + 8])
        size = TYPESIZE.get(typ, 0) * n
        if size == 0:
            continue
        if size <= 4:
            raw = blob[at + 8:at + 8 + size]
        else:
            where = struct.unpack(end + "I", blob[at + 8:at + 12])[0]
            raw = blob[where:where + size]
        if typ == 2:
            tags[tag] = raw.rstrip(b"\0").decode("latin-1")
        else:
            tags[tag] = list(struct.unpack(end + TYPEFMT[typ] * n, raw))
    return end, tags


def one(tags, tag, name):
    if tag not in tags:
        raise TiffError("%s (tag %d) is missing" % (name, tag))
    return tags[tag]


def pixels(blob, end, tags):
    """Decompress the strips back into one bytes object."""
    offsets = one(tags, STRIPOFFSETS, "StripOffsets")
    counts = one(tags, STRIPBYTECOUNTS, "StripByteCounts")
    comp = one(tags, COMPRESSION, "Compression")[0]
    if comp not in (1, 8, 32946):
        raise TiffError("unexpected compression %d" % comp)
    out = bytearray()
    for off, cnt in zip(offsets, counts):
        chunk = blob[off:off + cnt]
        out += chunk if comp == 1 else zlib.decompress(chunk)
    return bytes(out)



def zdr_value(byte):
    """A ZDR .wrk byte in dB.  The scale wraps at 81; palette.c holds the one
    definition the program uses, and tests/zdr-roundtrip.py is what keeps this
    copy and that one in step."""
    return (byte - 127) * 0.1 if byte >= 81 else (byte + 1) * 0.1


def check_zdr(data, nodata, source, say):
    """No cell may decode outside the range the source raster spanned.

    The reprojection fills single cell holes from their neighbours, so the
    image can hold bytes the source never had - but only ones whose value lies
    between two readings that were there.  A value beyond both ends means the
    fill happened in the byte, where the mean of 3.5 dB and 3.6 dB is -2.9.
    """
    path, _, member = source.partition(":")
    with zipfile.ZipFile(path) as z:
        raw = z.read(member)[8:]              # 8 byte map passport, then cells
    src = set(raw) - {0, nodata, 255}
    if not src:
        raise TiffError("%s carries no readings to compare against" % source)
    lo, hi = min(zdr_value(b) for b in src), max(zdr_value(b) for b in src)
    say("source %s spans %+.1f .. %+.1f dB" % (member, lo, hi))

    bad = {}
    for b in set(data) - {0, nodata, 255}:
        v = zdr_value(b)
        if v < lo - 1e-9 or v > hi + 1e-9:
            bad[b] = v
    if bad:
        raise TiffError("%d byte value(s) decode outside the source range: %s"
                        % (len(bad), ", ".join("%d=%+.1f dB" % (b, v)
                                               for b, v in sorted(bad.items()))))
    say("every byte decodes inside the range the source spanned")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--size", type=int)
    ap.add_argument("--nodata", type=int)
    ap.add_argument("--pixel", type=float)
    ap.add_argument("--origin")
    ap.add_argument("--min-data", type=int, default=1,
                    help="fail if fewer cells than this carry a reading")
    ap.add_argument("--zdr-source", metavar="ZIP:MEMBER",
                    help="the .wrk raster this ZDR product was built from. "
                         "Every byte in the image must decode to a value the "
                         "source already spanned: the reprojection interpolates, "
                         "and interpolating ZDR in the byte rather than in the "
                         "value used to invent readings of the opposite sign "
                         "(a hole between 3.5 and 3.6 dB averaged to -2.9)")
    args = ap.parse_args()

    blob = open(args.file, "rb").read()
    if len(blob) < 8:
        raise TiffError("file is %d bytes, too short to be a TIFF" % len(blob))
    end, tags = read_ifd(blob)
    say = lambda text: print("    %s" % text)

    width = one(tags, IMAGEWIDTH, "ImageWidth")[0]
    height = one(tags, IMAGELENGTH, "ImageLength")[0]
    if one(tags, BITSPERSAMPLE, "BitsPerSample")[0] != 8:
        raise TiffError("not 8 bits per sample")
    if one(tags, SAMPLESPERPIXEL, "SamplesPerPixel")[0] != 1:
        raise TiffError("not a single band")
    if one(tags, PHOTOMETRIC, "PhotometricInterpretation")[0] != 3:
        raise TiffError("not a palette image")
    say("%dx%d, 8 bit, one band, palette" % (width, height))
    if args.size and (width != args.size or height != args.size):
        raise TiffError("expected %dx%d" % (args.size, args.size))

    cmap = one(tags, COLORMAP, "ColorMap")
    if len(cmap) != 768:
        raise TiffError("colour map has %d entries, expected 768" % len(cmap))
    distinct = len(set(zip(cmap[0:256], cmap[256:512], cmap[512:768])))
    say("colour map: 256 entries, %d distinct" % distinct)
    if distinct < 3:
        raise TiffError("colour map looks empty (%d distinct)" % distinct)

    # georeferencing
    scale = one(tags, GEOPIXELSCALE, "ModelPixelScale")
    tie = one(tags, GEOTIEPOINTS, "ModelTiepoint")
    say("pixel size %g m, origin %g,%g" % (scale[0], tie[3], tie[4]))
    if args.pixel and abs(scale[0] - args.pixel) > 1e-6:
        raise TiffError("pixel size is %g, expected %g" % (scale[0], args.pixel))
    if args.origin:
        wx, wy = (float(v) for v in args.origin.split(","))
        if abs(tie[3] - wx) > 1e-6 or abs(tie[4] - wy) > 1e-6:
            raise TiffError("origin is %g,%g, expected %g,%g"
                            % (tie[3], tie[4], wx, wy))

    keys = one(tags, GEOKEYDIRECTORY, "GeoKeyDirectory")
    if len(keys) < 4 or keys[0] != 1:
        raise TiffError("bad GeoKeyDirectory header")
    found = {}
    for i in range(keys[3]):
        k, loc, cnt, val = keys[4 + i * 4:8 + i * 4]
        found[k] = val
    if found.get(1024) != 1:
        raise TiffError("GTModelType is %s, expected 1 (projected)"
                        % found.get(1024))
    if found.get(1025) != 1:
        raise TiffError("GTRasterType is %s, expected 1 (PixelIsArea)"
                        % found.get(1025))
    if found.get(3075) != 13:
        raise TiffError("ProjCoordTrans is %s, expected 13 (equidistant conic)"
                        % found.get(3075))
    say("%d geo keys, projected, PixelIsArea, equidistant conic" % keys[3])
    say("CRS: %s" % one(tags, GEOASCIIPARAMS, "GeoAsciiParams")[:72])

    nodata = int(one(tags, GDAL_NODATA, "GDAL_NODATA"))
    say("nodata %d" % nodata)
    if args.nodata is not None and nodata != args.nodata:
        raise TiffError("nodata is %d, expected %d" % (nodata, args.nodata))

    # the pixels themselves: a header can be perfect over an empty raster
    data = pixels(blob, end, tags)
    if len(data) != width * height:
        raise TiffError("%d pixel bytes, expected %d"
                        % (len(data), width * height))
    carrying = sum(1 for b in data if b != nodata)
    say("%d of %d cells carry a reading" % (carrying, width * height))
    if carrying < args.min_data:
        raise TiffError("only %d cells carry a reading, expected %d or more"
                        % (carrying, args.min_data))

    if args.zdr_source:
        check_zdr(data, nodata, args.zdr_source, say)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (TiffError, OSError, struct.error, ValueError, IndexError) as error:
        print("    FAIL: %s" % error)
        sys.exit(1)
