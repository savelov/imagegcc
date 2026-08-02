#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mkpalettes.py  -  generate config/palettes/*.pal from the pycao colour tables.

The viewer used to colour a map through config/palette (16 RGB triples) plus a
config/thresh.* file that mapped at most 15 data levels onto them.  The web
side (pycao, cao/palettes/*) has long since moved to per-product palettes with
far more levels, and geotiff.py already turns one of those into a 256 entry
colour map:

    palette = Palette(product_type=GetProductType(product))
    for i in range(256):
        cmap[i] = tuple(palette.get_img(conversion_func(i))[:3])

This script does the same thing for the C viewer.  For every product it walks
the 256 possible .wrk bytes, converts each to a physical value with the same
cao.conversion function the web side uses, looks the value up in the pycao
palette and writes the result as a .pal file the viewer loads at run time.
Python stays the single source of truth for the colours; the viewer gains no
runtime dependency on it.

    ./mkpalettes.py [--pycao DIR] [--out DIR]

Defaults: --pycao ../pycao_numba, --out config/palettes.

The generated files are CP866, like every other text file this program reads
(see config/cp866-8x14.psf); the unicode in this script is encoded on the way
out, so the script itself stays UTF-8.
"""

import argparse
import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))


def install_numba_shim():
    """cao.conversion decorates its converters with numba.vectorize.

    Only the scalar behaviour matters here - one byte in, one float out - so a
    shim keeps this script runnable without numba installed, which is the
    normal state of a machine that just builds the viewer.
    """
    try:
        import numba                                   # noqa: F401
        return
    except ImportError:
        pass

    shim = types.ModuleType("numba")

    class _Type(object):
        def __call__(self, *args):
            return self

    shim.double = _Type()
    shim.uint8 = _Type()
    shim.float64 = _Type()

    def vectorize(*args, **kwargs):
        def decorate(func):
            return func
        return decorate

    shim.vectorize = vectorize
    sys.modules["numba"] = shim


# ---------------------------------------------------------------- products
#
# One entry per palette the viewer can load.  `convert` is the byte -> physical
# value function; for the products this program actually reads it is the
# AKSOPRI one, because a .wrk byte is what bufr2wrk.py wrote, not a DMRL volume
# byte.  `nodata`/`noecho` name the bytes the viewer must treat as empty - they
# differ per product (see bufr2wrk.py: t_vel emits 255, t_dif's wrap makes 121
# the missing marker, everything else uses the traditional 254).
#
# The last group has no .wrk product behind it today.  Their palettes are
# generated anyway, from the DMRL converters, so that adding the product later
# is a table entry rather than a new colour scheme.

PHENOMENA_LABELS = [
    "нет обл",  "обл ср",   "слоист",   "осад сл",  "осад ум",
    "осад сил", "кучевая",  "ливень сл", "ливень ум", "ливен сил",
    "гроза (R)", "гроза R)", "гроза R",  "град сл",  "град ум",
    "град сил", "шквал сл", "шквал ум", "шквал сил", "смерч",
]


class Entry(object):
    """One palette.  `scale` only affects the legend text: the height palette
    is defined in metres and the viewer talks in kilometres."""

    def __init__(self, name, product_type, convert, units,
                 nodata=254, noecho=0, labels=None, scale=1.0, wired=True):
        self.name = name
        self.product_type = product_type
        self.convert = convert
        self.units = units
        self.nodata = nodata
        self.noecho = noecho
        self.labels = labels
        self.scale = scale
        self.wired = wired      # a maps[] entry in files.c reads this one


def product_table(conversion, ProductType):
    a = conversion.AksopriConverter
    d = conversion.DMRLConverter
    PT = ProductType
    return [
        Entry("reflectivity", PT.reflectivity_product,
              a[PT.reflectivity_product], "dBZ"),
        # bufr2wrk.py writes 255 for a point with no reading in these two.  It
        # did not always: the run length tables used to saturate instead, to
        # 221 (1431 mm/h) for the rain rate and, once the sum table wraps past
        # 254, to 14 (0.12 mm) for the sums - and debufr.exe gave the rain rate
        # 164 before the pipeline changed over in July 2026.  The viewer folds
        # those onto 255 as it reads a file, so the palette only has to know
        # the current one.  See the maps[] table in files.c.
        Entry("precip_rate", PT.precipitation_intensity_product,
              a[PT.precipitation_intensity_product], "мм/ч", nodata=255),
        Entry("precip_sum", PT.precipitation_sum_product,
              a[PT.precipitation_sum_product], "мм", nodata=255),
        Entry("zdr", PT.differential_reflectivity_product,
              a[PT.differential_reflectivity_product], "дБ", nodata=121),
        Entry("velocity", PT.velocity_product,
              a[PT.velocity_product], "м/с", nodata=255),
        Entry("height", PT.height_product,
              a[PT.height_product], "км", scale=0.001),
        # 4_myavl.wrk stores the phenomena code itself, which is exactly what
        # the pycao phenomena palette is keyed on - hence the identity
        # conversion, and hence the switch away from 4_storm.wrk (whose bytes
        # are the old 0/3/5/../60 severity scale this palette does not know).
        Entry("phenomena", PT.phenomena_product, None, "",
              noecho=None, labels=PHENOMENA_LABELS),

        # No .wrk product carries these yet, so they are not written unless
        # --all is given: a generated file nothing reads is just something to
        # go stale.  They are listed so that wiring one up later is a maps[]
        # entry in files.c rather than a new colour scheme.  The DMRL
        # converters are the only byte -> value definition available for them.
        Entry("spectrum_width", PT.spectrum_width_product,
              d[PT.spectrum_width_product], "м/с", nodata=255, wired=False),
        Entry("kdp", PT.kdp_product, d[PT.kdp_product], "гр/км",
              nodata=255, wired=False),
        Entry("rhohv", PT.correlation_coefficient_product,
              d[PT.correlation_coefficient_product], "", nodata=255, wired=False),
        Entry("vil", PT.vil_product, d[PT.vil_product], "кг/м2",
              nodata=255, wired=False),
        Entry("turbulence", PT.turbulence_product,
              d[PT.turbulence_product], "", nodata=255, wired=False),
        Entry("wind_shear", PT.wind_shear_product,
              d[PT.wind_shear_product], "", nodata=255, wired=False),
        Entry("angle", PT.angle_product, d[PT.angle_product], "гр",
              nodata=255, wired=False),
    ]


# ---------------------------------------------------------------- formatting

def fmt(value):
    """Shortest readable form of a threshold - the legend column is narrow."""
    if value == int(value):
        return "%d" % int(value)
    text = "%.3f" % value
    return text.rstrip("0").rstrip(".")


def band_label(thresholds, k, scale):
    """Label for the k-th colour of a step palette.

    Colour k covers (thresholds[k-1], thresholds[k]]; the first colour covers
    everything below the first threshold and the last one everything above the
    last.  ".." rather than "-" as the separator, because half of these ranges
    are negative.
    """
    if k == 0:
        return "<=" + fmt(thresholds[0] * scale)
    if k == len(thresholds):
        return ">" + fmt(thresholds[-1] * scale)
    return fmt(thresholds[k - 1] * scale) + ".." + fmt(thresholds[k] * scale)


# ---------------------------------------------------------------- generation

def build(entry, Palette, NO_DATA, NO_ECHO):
    """Return (lut, legend) for one product.

    lut     - 256 (r, g, b, legend row) tuples, indexed by .wrk byte
    legend  - [(r, g, b, label)], strongest first, then no echo and no data

    Every byte carries the legend row it belongs to, so that the viewer can
    label a reading without having to invert the colour - two rows of a palette
    may well share one colour (precipitation sums have two black bands).
    """
    convert, nodata, noecho, labels = (entry.convert, entry.nodata,
                                       entry.noecho, entry.labels)
    palette = Palette(product_type=entry.product_type)

    thresholds = list(palette.thresholds[2:])
    colours = [tuple(int(c) for c in rgb[:3]) for rgb in palette.colors[2:]]
    no_data_rgb = (0, 0, 0)                   # the map background
    no_echo_rgb = tuple(int(c) for c in palette.colors[1][:3])

    NODATA, NOECHO = -1, -2                   # placeholders, resolved below
    band = []
    used = [False] * len(colours)
    for byte in range(256):
        if byte == nodata or byte == 255:
            band.append(NODATA)
            continue
        if noecho is not None and byte == noecho:
            band.append(NOECHO)
            continue
        if labels is not None and byte >= len(labels):
            # a code table: bufr2wrk writes the no-data byte for anything the
            # table does not name (see t_myavl), so the rest cannot occur
            band.append(NODATA)
            continue
        value = float(byte) if convert is None else float(convert(byte))
        if value == NO_DATA:
            band.append(NODATA)
            continue
        if value == NO_ECHO:
            band.append(NOECHO)
            continue
        index = int(palette.thresholds.searchsorted(value)) - 2
        index = max(0, min(index, len(colours) - 1))
        used[index] = True
        band.append(index)

    legend = []
    row_of_band = {}
    for k in reversed(range(len(colours))):
        if not used[k]:
            continue                          # no byte can produce this colour
        if labels is not None and k < len(labels):
            label = labels[k]
        else:
            label = band_label(thresholds, k, entry.scale)
        row_of_band[k] = len(legend)
        legend.append(colours[k] + (label,))
    if noecho is not None:
        row_of_band[NOECHO] = len(legend)
        legend.append(no_echo_rgb + ("нет эхо",))
    row_of_band[NODATA] = len(legend)
    legend.append(no_data_rgb + ("нет инф",))

    lut = []
    for b in band:
        row = row_of_band.get(b, row_of_band[NODATA])
        lut.append(legend[row][:3] + (row,))
    return lut, legend


def write_pal(path, entry, lut, legend, source):
    out = []
    out.append("# generated by mkpalettes.py from %s - do not edit" % source)
    out.append("palette %s" % entry.name)
    out.append("units %s" % entry.units)
    out.append("nodata %d" % entry.nodata)
    out.append("noecho %d" % (-1 if entry.noecho is None else entry.noecho))
    out.append("colors 256")
    for byte, (r, g, b, row) in enumerate(lut):
        out.append("%3d %3d %3d %3d %2d" % (byte, r, g, b, row))
    out.append("legend %d" % len(legend))
    for r, g, b, label in legend:
        out.append("%3d %3d %3d :%s" % (r, g, b, label))
    out.append("end")

    text = "\n".join(out) + "\n"
    with open(path, "wb") as handle:
        handle.write(text.encode("cp866"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pycao", default=os.path.join(HERE, "..", "pycao_numba"),
                        help="directory holding the cao package")
    parser.add_argument("--out", default=os.path.join(HERE, "config", "palettes"),
                        help="where to write the .pal files")
    parser.add_argument("--all", action="store_true",
                        help="also write the palettes no .wrk product feeds yet")
    args = parser.parse_args()

    install_numba_shim()
    sys.path.insert(0, os.path.abspath(args.pycao))
    try:
        from cao.palette import Palette, PALETTES_DIR
        from cao.product import ProductType
        from cao.conversion import NO_DATA, NO_ECHO
        import cao.conversion as conversion
    except ImportError as error:
        sys.exit("cannot import the cao package from %s: %s" % (args.pycao, error))

    os.makedirs(args.out, exist_ok=True)
    for entry in product_table(conversion, ProductType):
        if not (entry.wired or args.all):
            continue
        source = os.path.join(PALETTES_DIR, ProductType.to_string(entry.product_type))
        lut, legend = build(entry, Palette, NO_DATA, NO_ECHO)
        path = os.path.join(args.out, entry.name + ".pal")
        write_pal(path, entry, lut, legend, os.path.relpath(source, args.pycao))
        print("%-16s %2d legend rows, widest label %2d -> %s"
              % (entry.name, len(legend), max(len(r[3]) for r in legend), path))


if __name__ == "__main__":
    main()
