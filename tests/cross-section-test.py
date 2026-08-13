#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check the vertical section the Python bindings hand out.

The section is the third reader of a product byte, after the map and the
GeoTIFF.  Whether the three agree on the ZDR scale is zdr-roundtrip.py's
question - it allows exactly one definition of it; this one is about the
raster that comes out of the bindings.  On tests/data:

  * a section comes out at all, for ZDR and for reflectivity, and None for a
    family the frame does not carry two levels of;
  * every byte in it decodes to a value the levels it was cut through already
    spanned - an interpolation cannot leave the range of its inputs, so a
    value outside it means the arithmetic happened in the byte;
  * the colours are the palette's, byte for byte, so a section and a map of
    one frame cannot disagree about what a colour means;
  * cross_section(values=True) describes that same raster: a cell is empty in
    both views or in neither, and every value agrees with the byte it was
    quantised to.  The web API serves the numbers and the picture from one
    cut, so the day they drift apart is the day it lies about one of them;
  * png() writes a PNG that reads back as the pixels it was given.

    python3 tests/cross-section-test.py [-v]

Needs libimage.so (./make.sh) and tests/data (tests/mkdata.py).  Nothing else:
the PNG is read back with zlib, as it was written.
"""

import datetime
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

import pyimage                                          # noqa: E402

FRAME = datetime.datetime(2026, 8, 10, 18, 50)          # Tyumen, ten ZDR levels
LINE = (985, 604, 1031, 685)                            # across the echo
PATHS = os.path.join(ROOT, "paths-cross-test")


def value_of(family, byte):
    """The byte scales, as crosssect.c has them.  ZDR wraps at 81; palette.c
    holds the one definition the program uses and tests/zdr-roundtrip.py is
    what keeps it honest."""
    if family == pyimage.FAM_ZDR:
        return (byte - 127) * 0.1 if byte >= 81 else (byte + 1) * 0.1
    if family == pyimage.FAM_DBZ:
        return byte / 3.0
    return (byte - 127) / 2.0


def levels_span(frame, family, product, nodata):
    """The range of values the frame's levels of one family cover."""
    lo, hi = None, None
    for level in range(1, 17):
        name = "%s%d" % (product, level)
        if name not in pyimage.PRODUCTS:
            continue
        try:
            frame.load(name)
        except pyimage.ImageError:
            continue
        for byte in set(frame.grid()):
            if byte in (0, nodata, 255):
                continue
            v = value_of(family, byte)
            lo = v if lo is None else min(lo, v)
            hi = v if hi is None else max(hi, v)
    return lo, hi


def read_png(path):
    """(width, height, palette, pixels) out of a palette PNG."""
    blob = open(path, "rb").read()
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("not a PNG: %r" % blob[:8])
    pos, chunks = 8, {}
    idat = bytearray()
    while pos < len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        tag = blob[pos + 4:pos + 8]
        payload = blob[pos + 8:pos + 8 + length]
        (crc,) = struct.unpack(">I", blob[pos + 8 + length:pos + 12 + length])
        if crc != zlib.crc32(tag + payload) & 0xffffffff:
            raise AssertionError("bad CRC on chunk %s" % tag.decode())
        if tag == b"IDAT":
            idat += payload
        else:
            chunks[tag] = payload
        pos += 12 + length
    width, height, depth, colour = struct.unpack(">IIBB", chunks[b"IHDR"][:10])
    if (depth, colour) != (8, 3):
        raise AssertionError("expected 8 bit palette, got depth %d type %d"
                             % (depth, colour))
    raw = zlib.decompress(bytes(idat))
    pixels = bytearray()
    for row in range(height):
        start = row * (width + 1)
        if raw[start] != 0:
            raise AssertionError("row %d uses filter %d" % (row, raw[start]))
        pixels += raw[start + 1:start + 1 + width]
    return width, height, chunks[b"PLTE"], bytes(pixels), chunks.get(b"tRNS")


def main():
    verbose = "-v" in sys.argv
    if not os.path.exists(os.path.join(HERE, "data")):
        sys.exit("tests/data is missing - run python3 tests/mkdata.py")
    with open(PATHS, "w") as handle:
        handle.write("WRK RAB\nMAP tests/data\nCFG config\nGRF config\n")

    problems = []
    try:
        archive = pyimage.Archive(os.path.basename(PATHS), workdir=ROOT)
    except pyimage.ImageError as error:
        os.unlink(PATHS)
        sys.exit("cannot open the fixture: %s" % error)

    try:
        frame = archive.nearest(FRAME)
        if abs((frame.timestamp - FRAME).total_seconds()) > 60:
            problems.append("no %s frame in the fixture, got %s"
                            % (FRAME, frame.timestamp))
        frame.load("zdr1")
        nodata = frame.info["nodata"]

        for family, product, name in ((pyimage.FAM_ZDR, "zdr", "zdr"),
                                      (pyimage.FAM_DBZ, "dbz", "dbz")):
            frame.load(product + "1")
            empty = frame.info["nodata"]
            section = frame.cross_section(*LINE, family=name)
            if section is None:
                problems.append("no %s section, though the frame carries "
                                "levels of it" % name)
                continue
            print("%-4s %s" % (name, section))

            if len(section.data) != section.width * section.height:
                problems.append("%s: %d bytes for a %dx%d raster"
                                % (name, len(section.data), section.width,
                                   section.height))
            if len(section.palette) != 768:
                problems.append("%s: palette is %d bytes"
                                % (name, len(section.palette)))

            # the colours are the map's colours
            palfile = os.path.join(ROOT, "config", "palettes",
                                   frame.info["palette"] + ".pal")
            wanted = {}
            for line in open(palfile, "rb").read().decode("cp866").splitlines():
                if line.startswith("legend"):
                    break                       # the labels below are not rows
                bits = line.split()
                if len(bits) == 5 and all(b.isdigit() for b in bits[:4]):
                    wanted[int(bits[0])] = tuple(int(v) for v in bits[1:4])
            wrong = [b for b, rgb in wanted.items()
                     if b != empty and tuple(section.palette[b * 3:b * 3 + 3]) != rgb]
            if wrong:
                problems.append("%s: %d palette entries differ from %s (%s)"
                                % (name, len(wrong), os.path.basename(palfile),
                                   wrong[:6]))

            # no byte may decode outside what the levels themselves spanned
            lo, hi = levels_span(frame, family, product, empty)
            if lo is None:
                problems.append("%s: the frame carries no readings" % name)
            else:
                out = sorted({b for b in set(section.data)
                              if b not in (empty, 255)
                              and not lo - 1e-9 <= value_of(family, b) <= hi + 1e-9})
                if verbose:
                    print("     levels span %+.1f .. %+.1f, section uses %d bytes"
                          % (lo, hi, len(set(section.data))))
                if out:
                    problems.append("%s: %s decode outside %+.1f..%+.1f"
                                    % (name, ["%d=%+.1f" % (b, value_of(family, b))
                                              for b in out[:6]], lo, hi))

            # the float export describes the same grid as the bytes.  Cells
            # are empty in both or in neither - that is the crisp one, and it
            # is what caught the section reading no-data off the loaded
            # product instead of off the family being cut.  The values
            # themselves must agree with what the bytes decode to, within the
            # quantisation of one byte, or the two scales have drifted apart.
            frame.load(product + "1")
            numbers = frame.cross_section(*LINE, family=name, values=True)
            if numbers is None or numbers.values is None:
                problems.append("%s: no values from cross_section(values=True)"
                                % name)
            else:
                if len(numbers.values) != numbers.width * numbers.height:
                    problems.append("%s: %d values for a %dx%d raster"
                                    % (name, len(numbers.values),
                                       numbers.width, numbers.height))
                if len(numbers.floor_km) != numbers.width:
                    problems.append("%s: %d beam floors for %d columns"
                                    % (name, len(numbers.floor_km),
                                       numbers.width))
                blank = [i for i, v in enumerate(numbers.values)
                         if (v <= numbers.NO_VALUE) != (numbers.data[i] == empty)]
                if blank:
                    problems.append("%s: %d cells empty as a byte but not as a "
                                    "value, or the other way round (%s)"
                                    % (name, len(blank), blank[:6]))
                step = {"zdr": 0.1, "dbz": 1 / 3.0}[name]
                drift = [(i, numbers.values[i], value_of(family, numbers.data[i]))
                         for i, v in enumerate(numbers.values)
                         if v > numbers.NO_VALUE and numbers.data[i] != empty
                         and abs(v - value_of(family, numbers.data[i])) > step]
                if verbose:
                    filled = sum(1 for v in numbers.values
                                 if v > numbers.NO_VALUE)
                    print("     %d/%d cells carry a value, in %s"
                          % (filled, len(numbers.values), numbers.units))
                if drift:
                    problems.append("%s: %d values disagree with the byte they "
                                    "were quantised to, e.g. %s"
                                    % (name, len(drift),
                                       ["#%d %.2f vs %.2f" % d for d in drift[:4]]))

            # and it writes a PNG that reads back
            frame.load(product + "1")
            png = os.path.join(HERE, "cross-%s.png" % name)
            section.png(png)
            w, h, plte, pixels, trns = read_png(png)
            if (w, h) != (section.width, section.height):
                problems.append("%s: PNG is %dx%d, section is %dx%d"
                                % (name, w, h, section.width, section.height))
            if pixels != section.data:
                problems.append("%s: PNG pixels differ from the raster" % name)
            if plte != section.palette:
                problems.append("%s: PNG palette differs from the section's" % name)
            if not trns or trns[empty] != 0:
                problems.append("%s: no-data byte %d is not transparent"
                                % (name, empty))
            if verbose:
                print("     %s: %d bytes, reads back identical"
                      % (os.path.basename(png), os.path.getsize(png)))
            os.unlink(png)

        # a family the frame has no levels of
        frame.load("zdr1")
        if frame.cross_section(*LINE, family="vel") is not None:
            problems.append("cut a velocity section through a frame that "
                            "carries no velocity")
    finally:
        archive.close()
        os.path.exists(PATHS) and os.unlink(PATHS)

    print()
    if problems:
        print("FAIL:")
        for p in problems:
            print("   " + p)
        return 1
    print("PASS: sections come out, stay inside the range of the levels they "
          "cut, wear the map's colours, agree with their own values and "
          "survive the PNG")
    return 0


if __name__ == "__main__":
    sys.exit(main())
