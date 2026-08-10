#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""End-to-end check of the ZDR chain: uvknew -> BUFR -> .wrk -> viewer.

A reading survives four transformations before it reaches the screen, each
written at a different time by different people:

  1. uvknew holds ZDR as an internal byte.  utablcolor.cpp ByteToVal case 22:
         ZDR = -12.70 + (v-1)*0.10          v = 1..255  ->  -12.7 .. +12.7 dB
  2. uformpublicair.cpp EncodeZDR turns that into the BUFR 021003 raw value:
         znath = round(ZDR*10) + 5
     held in an unsigned short and written as the low 7 bits of the field.
     The range guard there is `if ((znath<0) && (znath>255)) znath = 0;`
     which no value can satisfy, so nothing is ever clamped.
  3. debufr maps raw to the .wrk byte through the table bufr2wrk.py calls
     _DIF, which this script imports rather than copies.
  4. the viewer reads the byte back - showdata.c FAM_ZDR, and the same
     formula in cao/conversion.py, which is where the colour comes from.

Step 2 is where the range is lost.  BUFR 021003 is 7 bits with reference -5,
so it can only carry ZDR -0.4 .. +12.2 dB; anything outside wraps into that
window and becomes indistinguishable from a reading that belongs there.

This script pushes all 256 internal values through the chain and reports what
comes back.  It asserts nothing about the wrapped region, because there is
nothing to assert - the byte genuinely cannot say which reading it holds.
What it does assert is that the representable range round-trips within the
quantisation debufr's table imposes.

It also checks the second reader of these bytes.  The vertical cross section
(crosssect.c) decodes a byte to interpolate it and then encodes the result
back, because the palette is indexed by the byte and not by the value; the
pair has to use the same split as showdata.c and has to be a round trip.  It
was not: the section split at 123, so bytes 81..120 read as +8.2..+12.1 dB
while the palette coloured them -4.6..-0.7, and the plot printed 9.1 dB under
a cursor sitting on a deep blue cell.

    python3 tests/zdr-roundtrip.py [-v]
"""

import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# The split the viewer uses, read out of showdata.c so this cannot drift.
SPLIT_RE = re.compile(rb'v=\(value>=(\d+)\)\s*\?\s*\(value-(\d+)\)\*0\.1'
                      rb'\s*:\s*\(value\+(\d+)\)\*0\.1')


def viewer_decode():
    """(split, lo_off, hi_off) as showdata.c has them."""
    src = open(os.path.join(ROOT, 'showdata.c'), 'rb').read()
    m = SPLIT_RE.search(src)
    if not m:
        sys.exit('cannot find the FAM_ZDR formula in showdata.c')
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


# The same split again in the cross section, plus the inverse that turns an
# interpolated value back into a byte for the palette to colour.
SECTION_RE = re.compile(rb'return byte>=(\d+)\s*\?\s*\(byte-(\d+)\)\*0\.1f'
                        rb'\s*:\s*\(byte\+(\d+)\)\*0\.1f')
INVERSE_RE = re.compile(rb'byte=\(int\)floor\(value\*10\.0\+0\.5\);\s*'
                        rb'if\s*\(byte>=(\d+)\)\s*\{\s*byte-=1;\s*'
                        rb'if\s*\(byte>(\d+)\)\s*byte=(\d+);\s*'
                        rb'\}\s*else\s*\{\s*byte\+=127;\s*'
                        rb'if\s*\(byte<(\d+)\)\s*byte=(\d+);\s*\}\s*'
                        rb'if\s*\(byte==(\d+)\)\s*byte=(\d+);')


def strip_comments(src):
    """C comments out, so a regex can see two statements as adjacent."""
    return re.sub(rb'/\*.*?\*/', b' ', src, flags=re.S)


def section_formulas():
    """(split, hi_off, lo_off, top, bottom, nodata, nodata_to) from crosssect.c."""
    src = strip_comments(open(os.path.join(ROOT, 'crosssect.c'), 'rb').read())
    dec = SECTION_RE.search(src)
    if not dec:
        sys.exit('cannot find cs_value FAM_ZDR in crosssect.c')
    enc = INVERSE_RE.search(src)
    if not enc:
        sys.exit('cannot find the FAM_ZDR case of cross_section_byte '
                 'in crosssect.c')
    first, top, top2, bottom, bottom2, nodata, nodata_to = (int(g)
                                                            for g in enc.groups())
    if top != top2 or bottom != bottom2:
        sys.exit('the cross section clamps to a byte it did not test against')
    return (int(dec.group(1)), int(dec.group(2)), int(dec.group(3)),
            first, top, bottom, nodata, nodata_to)


def check_section(split, hi_off, lo_off, DIF, verbose):
    """The section must read a byte as showdata.c does and write it back."""
    (s_split, s_hi, s_lo, first, top, bottom,
     nodata, nodata_to) = section_formulas()

    problems = []
    if (s_split, s_hi, s_lo) != (split, hi_off, lo_off):
        problems.append('crosssect.c decodes with (split %d, -%d, +%d), '
                        'showdata.c with (split %d, -%d, +%d)'
                        % (s_split, s_hi, s_lo, split, hi_off, lo_off))

    def value(byte):
        return (byte - s_hi) * 0.10 if byte >= s_split else (byte + s_lo) * 0.10

    def encode(v):
        b = int(math.floor(v * 10.0 + 0.5))
        if b >= first:
            b -= 1
            if b > top:
                b = top
        else:
            b += 127
            if b < bottom:
                b = bottom
        return nodata_to if b == nodata else b

    # every byte debufr's table can write, back to itself
    for byte in sorted(set(DIF[1:])):
        if byte == nodata:
            continue                          # not a reading, it is the hole
        back = encode(value(byte))
        if back != byte:
            problems.append('byte %d reads %+.1f dB and encodes back to %d'
                            % (byte, value(byte), back))

    # and the clamps: off either end of the scale, not across the wrap
    for v, want in ((99.0, top), (-99.0, bottom)):
        if encode(v) != want:
            problems.append('%+.0f dB clamps to byte %d, not %d'
                            % (v, encode(v), want))

    if verbose:
        print('  cross section split at byte %d, continuation %d..%d, '
              'linear %d..%d' % (s_split, first - 1, top, bottom,
                                 max(DIF[1:])))
    return problems


def dif_table():
    """_DIF straight out of bufr2wrk.py (cp866 source, so decode it)."""
    src = open(os.path.join(ROOT, 'bufr2wrk.py'), 'rb').read().decode('cp866')
    m = re.search(r'_DIF\s*=\s*(\[[^\]]*\])', src)
    if not m:
        sys.exit('cannot find _DIF in bufr2wrk.py')
    return eval(m.group(1))


def main():
    verbose = '-v' in sys.argv
    split, hi_off, lo_off = viewer_decode()
    DIF = dif_table()

    def uvknew_zdr(v):                       # step 1
        return -12.70 + (v - 1) * 0.10

    def encode(zdr, internal):               # step 2
        if internal == 0:
            return 0                         # no echo, set explicitly
        znath = int(round(zdr * 10.0)) + 5
        return znath & 0x7F                  # unsigned short -> 7 bit field

    def decode(byte):                        # step 4
        if byte == 0:
            return 'no echo'
        if byte in (121, 255):
            return 'no data'
        if byte >= split:
            return (byte - hi_off) * 0.10
        return (byte + lo_off) * 0.10

    exact = quantised = lost = 0
    rows = []
    for v in range(1, 256):
        true = round(uvknew_zdr(v), 2)
        raw = encode(true, v)
        byte = DIF[raw] if raw < len(DIF) else None
        shown = decode(byte) if byte is not None else 'n/a'
        if isinstance(shown, str):
            verdict = 'LOST (%s)' % shown
            lost += 1
        else:
            err = abs(shown - true)
            if err < 0.05:
                verdict = 'exact'
                exact += 1
            elif err <= 0.25:
                verdict = 'quantised %.1f dB' % err
                quantised += 1
            else:
                verdict = 'WRONG by %.1f dB' % err
                lost += 1
        rows.append((v, true, raw, byte, shown, verdict))

    if verbose:
        print('  int   true ZDR   raw   byte   shown      verdict')
        for r in rows:
            shown = r[4] if isinstance(r[4], str) else '%+6.1f' % r[4]
            print('  %3d   %+7.1f   %3d   %4s   %-9s  %s'
                  % (r[0], r[1], r[2], r[3], shown, r[5]))

    # what actually survives
    good = [r[1] for r in rows if r[5] == 'exact' or r[5].startswith('quant')]
    print('uvknew internal values checked: 255')
    print('  exact           %3d' % exact)
    print('  quantised <=0.2 %3d' % quantised)
    print('  lost or wrong   %3d' % lost)
    if good:
        print('  faithful range  %+.1f .. %+.1f dB' % (min(good), max(good)))
    print('  viewer split at byte %d (showdata.c)' % split)
    section = check_section(split, hi_off, lo_off, DIF, verbose)

    # The contract: everything the 7 bit field can actually hold must survive.
    # That is raw 1..127 with the reference of -5, i.e. -0.4 .. +12.2 dB,
    # minus the part the split hands to the underflow reading.
    representable = [r for r in rows if -0.4 - 1e-9 <= r[1] <= (split - 1 - 5) / 10.0]
    bad = [r for r in representable if r[5].startswith('WRONG') or r[5].startswith('LOST')]
    print()
    if bad:
        print('FAIL: %d values inside the representable range did not survive:'
              % len(bad))
        for r in bad[:10]:
            print('   internal %3d  true %+5.1f  raw %3d  byte %3s  shown %s'
                  % (r[0], r[1], r[2], r[3], r[4]))
        return 1
    if section:
        print('FAIL: the cross section does not read these bytes the way the '
              'map does:')
        for p in section[:10]:
            print('   ' + p)
        return 1
    print('PASS: every ZDR the BUFR field can carry survives the chain, '
          'within debufr quantisation, and the cross section agrees with the '
          'map on every byte')
    return 0


if __name__ == '__main__':
    sys.exit(main())
