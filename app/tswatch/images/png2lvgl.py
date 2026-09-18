#!/usr/bin/env python3
"""Convert PNG files to LVGL v9 C image arrays (ARGB8888)."""
import os, sys, struct
from PIL import Image

MAGIC = 0x19
CF_ARGB8888 = 0x10

def convert_png_to_lvgl_c(png_path, c_name):
    img = Image.open(png_path).convert("RGBA")
    w, h = img.size
    pixels = list(img.getdata())  # list of (R, G, B, A)

    # LVGL ARGB8888 stores as BGRA in memory (little-endian)
    data = bytearray()
    stride = w * 4
    for r, g, b, a in pixels:
        data.extend([b, g, r, a])

    c_lines = []
    c_lines.append(f'/* Auto-generated from {os.path.basename(png_path)} — {w}x{h} ARGB8888 */')
    c_lines.append(f'#include <lvgl.h>')
    c_lines.append(f'')
    c_lines.append(f'const uint8_t {c_name}_map[] = {{')

    # Write ONLY pixel data (no header)
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
        c_lines.append(f'    {hex_bytes},')

    c_lines.append(f'}};')
    c_lines.append(f'')
    c_lines.append(f'const lv_image_dsc_t {c_name} = {{')
    c_lines.append(f'    .header = {{')
    c_lines.append(f'        .magic = LV_IMAGE_HEADER_MAGIC,')
    c_lines.append(f'        .cf = LV_COLOR_FORMAT_ARGB8888,')
    c_lines.append(f'        .flags = 0,')
    c_lines.append(f'        .w = {w},')
    c_lines.append(f'        .h = {h},')
    c_lines.append(f'        .stride = {stride},')
    c_lines.append(f'    }},')
    c_lines.append(f'    .data_size = sizeof({c_name}_map),')
    c_lines.append(f'    .data = {c_name}_map,')
    c_lines.append(f'}};')
    c_lines.append(f'')

    return '\n'.join(c_lines), w, h


def main():
    emmc_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
    out_dir = sys.argv[2] if len(sys.argv) > 2 else '.'

    c_files = []
    h_lines = ['/* Auto-generated LVGL image declarations */', '#pragma once', '#include <lvgl.h>', '']

    for fname in sorted(os.listdir(emmc_dir)):
        if not fname.endswith('.png'):
            continue
        c_name = fname.replace('.png', '').replace('-', '_')
        png_path = os.path.join(emmc_dir, fname)

        c_content, w, h = convert_png_to_lvgl_c(png_path, c_name)
        c_out = os.path.join(out_dir, f'{c_name}.c')
        with open(c_out, 'w') as f:
            f.write(c_content)
        c_files.append(c_out)
        h_lines.append(f'extern const lv_image_dsc_t {c_name};  /* {w}x{h} */')
        print(f'  {fname} -> {c_name}.c ({w}x{h})')

    h_out = os.path.join(out_dir, 'images.h')
    with open(h_out, 'w') as f:
        f.write('\n'.join(h_lines) + '\n')
    print(f'\nGenerated {len(c_files)} .c files + images.h in {out_dir}')


if __name__ == '__main__':
    main()
