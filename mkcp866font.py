#!/usr/bin/env python3
"""Build a CP866 ordered console font for GRX.

The map labels in config/*.k* are DOS CP866 text, but GRX only ships CP437
fonts, so every Cyrillic byte comes out as a random accented latin glyph.
This takes any PSF font that carries a unicode table, looks up the glyph for
each CP866 character and writes them out in CP866 order, which is what the
program's byte values index.

  usage: mkcp866font.py <source.psf[.gz]> <output.psf>
"""

import gzip
import struct
import sys

# Codes 0x00-0x1F and 0x7F are control characters as far as the cp866 codec is
# concerned, but a DOS screen showed them as these symbols and the program
# still uses some of them (the arrows, mainly).
CP437_CONTROL_GLYPHS = {
    0x01: "☺", 0x02: "☻", 0x03: "♥", 0x04: "♦",
    0x05: "♣", 0x06: "♠", 0x07: "•", 0x08: "◘",
    0x09: "○", 0x0A: "◙", 0x0B: "♂", 0x0C: "♀",
    0x0D: "♪", 0x0E: "♫", 0x0F: "☼", 0x10: "►",
    0x11: "◄", 0x12: "↕", 0x13: "‼", 0x14: "¶",
    0x15: "§", 0x16: "▬", 0x17: "↨", 0x18: "↑",
    0x19: "↓", 0x1A: "→", 0x1B: "←", 0x1C: "∟",
    0x1D: "↔", 0x1E: "▲", 0x1F: "▼", 0x7F: "⌂",
}

PSF1_MAGIC = b"\x36\x04"
PSF1_MODE512 = 0x01
PSF1_MODEHASTAB = 0x02
PSF2_MAGIC = b"\x72\xb5\x4a\x86"
PSF2_HAS_UNICODE = 0x01


def read_font(path):
    """Return (glyphs, width, height, unicode_map)."""
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rb") as fh:
        data = fh.read()

    if data[:2] == PSF1_MAGIC:
        mode, charsize = data[2], data[3]
        count = 512 if mode & PSF1_MODE512 else 256
        width, height = 8, charsize
        start = 4
        has_table = bool(mode & PSF1_MODEHASTAB)
        table_entry = 2          # 16 bit unicode values, 0xFFFF terminated
    elif data[:4] == PSF2_MAGIC:
        (_ver, hdrsize, flags, count,
         charsize, height, width) = struct.unpack_from("<7I", data, 4)
        start = hdrsize
        has_table = bool(flags & PSF2_HAS_UNICODE)
        table_entry = 0          # utf-8, 0xFF terminated
    else:
        raise SystemExit("%s: not a PSF font" % path)

    bytes_per_row = (width + 7) // 8
    charsize = bytes_per_row * height
    glyphs = [data[start + i * charsize: start + (i + 1) * charsize]
              for i in range(count)]

    umap = {}
    if has_table:
        pos = start + count * charsize
        for index in range(count):
            if table_entry == 2:
                while pos + 1 < len(data):
                    value = struct.unpack_from("<H", data, pos)[0]
                    pos += 2
                    if value == 0xFFFF:
                        break
                    umap.setdefault(value, index)
            else:
                entry = bytearray()
                while pos < len(data) and data[pos] != 0xFF:
                    entry.append(data[pos])
                    pos += 1
                pos += 1                     # skip the 0xFF terminator
                for ch in entry.split(b"\xfe"):   # ignore sequences
                    try:
                        text = ch.decode("utf-8")
                    except UnicodeDecodeError:
                        continue
                    if len(text) == 1:
                        umap.setdefault(ord(text), index)

    return glyphs, width, height, umap


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    source, target = sys.argv[1], sys.argv[2]

    glyphs, width, height, umap = read_font(source)
    if width != 8:
        raise SystemExit("%s is %d pixels wide, PSF1 output needs 8" % (source, width))
    if not umap:
        raise SystemExit("%s has no unicode table, cannot remap it" % source)

    blank = bytes(height)
    out = bytearray()
    missing = []
    for code in range(256):
        char = CP437_CONTROL_GLYPHS.get(code) or bytes([code]).decode("cp866")
        index = umap.get(ord(char))
        if index is None or index >= len(glyphs):
            missing.append(code)
            out += blank
        else:
            out += glyphs[index]

    with open(target, "wb") as fh:
        fh.write(PSF1_MAGIC + bytes([0, height]))    # mode 0: 256 glyphs
        fh.write(out)

    print("wrote %s: 256 glyphs, 8x%d, from %s" % (target, height, source))
    if missing:
        print("no glyph for %d code(s): %s" %
              (len(missing), " ".join("0x%02X" % c for c in missing)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
