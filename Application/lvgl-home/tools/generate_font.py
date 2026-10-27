#!/usr/bin/env python3
"""Generate a deterministic LVGL 8 1-bpp font; does not build/run the application."""
import argparse
from pathlib import Path
from PIL import ImageFont

parser = argparse.ArgumentParser()
parser.add_argument('--font', required=True, type=Path)
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'fonts/aiglass_font_16.c')
args = parser.parse_args()
font = ImageFont.truetype(str(args.font), 16, index=2)  # Noto Sans CJK SC
points = sorted(set(range(32, 127)) | set(range(0x3000, 0x3100)) |
                set(range(0x4E00, 0xA000)) | {0x00B0, 0x00B7, 0x00D7, 0x2026, 0xFF01,
                0xFF08, 0xFF09, 0xFF0C, 0xFF1A, 0xFF1B, 0xFF1F})
bitmap = bytearray()
descriptors = ['    {0}, /* Reserved glyph. */']
for point in points:
    character = chr(point)
    mask, offset = font.getmask2(character, mode='L', anchor='ls')
    width, height = mask.size
    pixels = bytes(mask)
    start = len(bitmap)
    for first in range(0, len(pixels), 8):
        value = 0
        for bit, pixel in enumerate(pixels[first:first + 8]):
            if pixel >= 96:
                value |= 1 << (7 - bit)
        bitmap.append(value)
    advance = round(font.getlength(character) * 16)
    descriptors.append('    {.bitmap_index=%d, .adv_w=%d, .box_w=%d, .box_h=%d, .ofs_x=%d, .ofs_y=%d},' %
                       (start, advance, width, height, offset[0], -offset[1] - height))
assert len(bitmap) < 1 << 20
# Two sparse cmaps because full-width punctuation lies beyond a 16-bit span.
groups = [[p for p in points if p < 0xF000], [p for p in points if p >= 0xF000]]
lines = ['/* Generated from Noto Sans CJK SC. Font data: SIL OFL 1.1; see OFL.txt. */',
         '#include "lvgl.h"', 'static const uint8_t bitmap[] = {']
for offset in range(0, len(bitmap), 24):
    lines.append('    ' + ','.join('0x%02x' % b for b in bitmap[offset:offset + 24]) + ',')
lines += ['};', 'static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {', *descriptors, '};']
for number, group in enumerate(groups):
    lines.append('static const uint16_t unicode_%d[] = {' % number)
    relative = [p - group[0] for p in group]
    for first in range(0, len(relative), 24):
        lines.append('    ' + ','.join(str(p) for p in relative[first:first + 24]) + ',')
    lines.append('};')
lines.append('static const lv_font_fmt_txt_cmap_t cmaps[] = {')
first_glyph = 1
for number, group in enumerate(groups):
    lines.append('    {.range_start=%d, .range_length=%d, .glyph_id_start=%d, .unicode_list=unicode_%d, .list_length=%d, .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},' %
                 (group[0], group[-1] - group[0] + 1, first_glyph, number, len(group)))
    first_glyph += len(group)
lines += ['};', 'static lv_font_fmt_txt_glyph_cache_t cache;',
          'static lv_font_fmt_txt_dsc_t description = {',
          '    .glyph_bitmap=bitmap, .glyph_dsc=glyphs, .cmaps=cmaps,',
          '    .cmap_num=2, .bpp=1, .bitmap_format=0, .cache=&cache', '};',
          'const lv_font_t aiglass_font_16 = {',
          '    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,',
          '    .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,',
          '    .line_height=22, .base_line=4, .dsc=&description,',
          '    .fallback=LV_FONT_DEFAULT', '};', '']
args.output.write_text('\n'.join(lines))
print(f'{args.output}: {len(points)} glyphs, {len(bitmap)} bitmap bytes')
