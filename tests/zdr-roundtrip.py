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

    python3 tests/zdr-roundtrip.py [-v]
"""

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
    print('PASS: every ZDR the BUFR field can carry survives the chain, '
          'within debufr quantisation')
    return 0


if __name__ == '__main__':
    sys.exit(main())
