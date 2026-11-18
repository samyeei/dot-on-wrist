#!/usr/bin/env python3
"""Rasterize a JIS level 1 Noto Sans JP subset as a LovyanGFX VLW font."""
import argparse
from pathlib import Path
import re
import struct

from PIL import ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('font', type=Path, help='Noto Sans JP TTF')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / 'firmware/src'
    source = (root / 'main.cpp').read_text()
    labels = re.findall(r'ANNA_TEXT\("[^"]*", "[^"]*", "([^"]*)"\)', source)
    chars = set(''.join(labels)) | {chr(code) for code in range(32, 127)}
    for row in range(0xA1, 0xD0):
        for col in range(0xA1, 0xFF):
            chars.update(bytes((row, col)).decode('euc_jp', errors='ignore'))

    font = ImageFont.truetype(str(args.font), 24)
    font.set_variation_by_axes([400])
    ascent, descent = font.getmetrics()
    records = bytearray()
    bitmaps = bytearray()
    for char in sorted(chars):
        mask, (left, top) = font.getmask2(char, mode='L', anchor='ls')
        width, height = mask.size
        advance = round(font.getlength(char))
        records.extend(struct.pack('>7i', ord(char), height, width, advance, -top, left, 0))
        bitmaps.extend(bytes(mask))

    header = struct.pack('>6i', len(chars), 11, 24, 0, ascent, descent)
    font_bytes = header + records + bitmaps
    with (root / 'ui_font_ja.cpp').open('w') as output:
        output.write('#include <stdint.h>\n\n#if defined(ANNA_LANG_JA)\n')
        output.write('// Noto Sans JP, 24 px grayscale. See ../licenses/NotoSansJP-OFL.txt.\n')
        output.write(f'extern const uint8_t annaJapaneseBitmap[{len(font_bytes) + 1}] =\n')
        for offset in range(0, len(font_bytes), 48):
            row = ''.join(f'\\{byte:03o}' for byte in font_bytes[offset:offset + 48])
            output.write(f'    "{row}"\n')
        output.write('    ;\n#endif\n')
    print(f'Japanese VLW font generated with {len(chars)} glyphs')


if __name__ == '__main__':
    main()
