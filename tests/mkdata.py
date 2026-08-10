#!/usr/bin/env python3
"""Build tests/data from the BUFR files in tests/bufr and tests/bufr-tyumen.

The archive the viewer reads is one zip per station per observation time,
named portN/YYMMDDHH.MMm, holding the .wrk product rasters plus header.wrk.
bufr2wrk.py turns one BUFR message into one product, so a frame is the set
of messages that share a station and a time.

This regenerates the committed fixture; it does not need to run in CI, and
deliberately depends on nothing outside the standard library (bufr2wrk.py
imports sys, os, struct, bisect and datetime only).

    python3 tests/mkdata.py           rebuild tests/data
    python3 tests/mkdata.py --list    just show what the BUFR files hold
"""

import os
import sys
import zipfile
import collections
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# One directory per station whose messages are kept.  Each holds its own
# DEBUFR.CFg: bufr2wrk.py reads the local time shift and the per station dBZ
# calibration out of the one beside the message, and they differ by feed.
BUFR = [os.path.join(HERE, "bufr"), os.path.join(HERE, "bufr-tyumen")]
DATA = os.path.join(HERE, "data")

# Which port directory each station goes to.  The viewer takes a port's
# position from header.wrk (files.c reads bytes 47..52 as degrees, minutes
# and seconds), so the number here only has to be a port the app draws -
# it does not have to be the station's operational port.
# Keyed on the station code bufr2wrk reads out of the message (the bulletin
# code, e.g. RAKD, not the WMO number).
PORT_FOR_STATION = {
    b"RAKD": 11,           # Krasnodar, WMO 39408
    b"RATN": 97,           # Tyumen, WMO 39692 - the ten ZDR levels
}


def load_bufr2wrk():
    """Import bufr2wrk.py from the repository root."""
    path = os.path.join(ROOT, "bufr2wrk.py")
    spec = importlib.util.spec_from_file_location("bufr2wrk", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def decode_all(b2w, tmpdir):
    """Decode every BUFR file; return {(station, stamp): {name: bytes}}."""
    frames = collections.defaultdict(dict)
    for directory in BUFR:
        print("decoding %s:" % directory)
        decode_dir(b2w, tmpdir, directory, frames)
    return frames


def decode_dir(b2w, tmpdir, directory, frames):
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".buf"):
            continue
        src = os.path.join(directory, name)
        out = os.path.join(tmpdir, name[:-4])
        os.makedirs(out, exist_ok=True)
        try:
            obs, fields, iprn, tkey, written = b2w.convert(src, out)
        except Exception as exc:                  # a message we cannot read
            print("  %-14s skipped (%s)" % (name, exc))
            continue
        if not written:
            print("  %-14s IPRN%s unsupported, nothing written" % (name, iprn))
            continue
        tme = b2w.read_cfg_tme(src)
        station = fields["_stcode"][0]
        stamp = (obs["year"], obs["month"], obs["day"],
                 (obs["hour"] + tme) % 24, obs["minute"])
        for path in written:
            frames[(station, stamp)][os.path.basename(path)] = \
                open(path, "rb").read()
        prods = [os.path.basename(p) for p in written
                 if not p.endswith("header.wrk")]
        print("  %-14s %s %s -> %s" % (name, station.decode(), stamp,
                                       ", ".join(prods)))


def write_frames(frames):
    total = 0
    for (station, stamp), members in sorted(frames.items()):
        port = PORT_FOR_STATION.get(station)
        if port is None:
            print("no port mapped for station %s, skipping" % station.decode())
            continue
        yy, mm, dd, hh, mi = stamp
        d = os.path.join(DATA, "port%d" % port)
        os.makedirs(d, exist_ok=True)
        fn = os.path.join(d, "%02d%02d%02d%02d.%02dm" % (yy, mm, dd, hh, mi))
        # Sorted names and a fixed member timestamp, so that rebuilding
        # unchanged inputs gives byte-identical zips and leaves the working
        # tree clean.  writestr() would otherwise stamp the current time
        # into every entry and the fixture would look modified after every
        # run.  1980-01-01 is the earliest a zip can represent.
        with zipfile.ZipFile(fn, "w", zipfile.ZIP_DEFLATED) as z:
            for name in sorted(members):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, members[name])
        n = len(members) - 1                      # header.wrk is not a product
        print("port%-3d %s  %2d products, %d bytes"
              % (port, os.path.basename(fn), n, os.path.getsize(fn)))
        total += 1
    return total


def main():
    b2w = load_bufr2wrk()
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        frames = decode_all(b2w, tmp)
        if "--list" in sys.argv:
            return 0
        print("\nwriting %s:" % DATA)
        n = write_frames(frames)
    print("\n%d frames written" % n)
    return 0 if n >= 2 else 1        # one frame alone renders empty


if __name__ == "__main__":
    sys.exit(main())
