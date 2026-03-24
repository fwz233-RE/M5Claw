"""
gen_sprites.py — Sunset scene generator for M5ClawPro
Reads SunSet1.png / SunSet2.png / SunSet3.png (3200x1800),
downscales to 240x135, builds a unified palette, and emits
src/sunset_data.h + src/sunset_data.cpp with PROGMEM arrays.

Usage:  python gen_sprites.py [--colors N]
"""

from PIL import Image
import os, sys, argparse

SCENE_W = 240
SCENE_H = 135

def rgb_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def generate(max_colors):
    base = os.path.dirname(os.path.abspath(__file__))
    names = ["SunSet1.png", "SunSet2.png", "SunSet3.png"]

    imgs = []
    for name in names:
        path = os.path.join(base, name)
        if not os.path.exists(path):
            print(f"ERROR: {path} not found"); sys.exit(1)
        img = Image.open(path).convert("RGB")
        print(f"  {name}: {img.size[0]}x{img.size[1]}")
        img = img.resize((SCENE_W, SCENE_H), Image.LANCZOS)
        imgs.append(img)

    combined = Image.new("RGB", (SCENE_W, SCENE_H * len(imgs)))
    for i, img in enumerate(imgs):
        combined.paste(img, (0, i * SCENE_H))

    print(f"Quantizing to {max_colors} colors...")
    quantized = combined.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT, dither=0)
    pal_flat = quantized.getpalette()

    used = set(quantized.getdata())
    pal_size = max(used) + 1
    palette = []
    for i in range(pal_size):
        palette.append((pal_flat[i*3], pal_flat[i*3+1], pal_flat[i*3+2]))
    print(f"Palette entries used: {pal_size}")

    pixels = list(quantized.getdata())
    sz = SCENE_W * SCENE_H
    scenes = [pixels[i*sz:(i+1)*sz] for i in range(len(imgs))]

    # ── header ──
    h_lines = [
        "#pragma once",
        "#include <cstdint>",
        "#include <pgmspace.h>",
        "",
        f"#define SCENE_PALETTE_SIZE {pal_size}",
        "",
        "extern const uint16_t scene_palette[];",
    ]
    for idx in range(1, len(imgs)+1):
        h_lines.append(f"extern const uint8_t  scene_pixels_{idx}[];")

    h_path = os.path.join(base, "src", "sunset_data.h")
    with open(h_path, "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines) + "\n")
    print(f"Written: {h_path}")

    # ── source ──
    c_lines = ['#include "sunset_data.h"', ""]

    c_lines.append(f"const uint16_t scene_palette[SCENE_PALETTE_SIZE] PROGMEM = {{")
    row = "  "
    for i, (r, g, b) in enumerate(palette):
        row += f"0x{rgb_to_rgb565(r,g,b):04X},"
        if (i + 1) % 12 == 0:
            c_lines.append(row)
            row = "  "
    if row.strip():
        c_lines.append(row)
    c_lines.append("};")
    c_lines.append("")

    for idx, sc in enumerate(scenes, 1):
        c_lines.append(f"const uint8_t scene_pixels_{idx}[{SCENE_W}*{SCENE_H}] PROGMEM = {{")
        for y in range(SCENE_H):
            off = y * SCENE_W
            c_lines.append("  " + ",".join(str(sc[off+x]) for x in range(SCENE_W)) + ",")
        c_lines.append("};")
        c_lines.append("")

    cpp_path = os.path.join(base, "src", "sunset_data.cpp")
    with open(cpp_path, "w", encoding="utf-8") as f:
        f.write("\n".join(c_lines) + "\n")
    print(f"Written: {cpp_path}  ({len(c_lines)} lines)")
    print("Done.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate sunset scene data")
    parser.add_argument("--colors", type=int, default=64,
                        help="Max palette colors (default 64)")
    args = parser.parse_args()
    generate(args.colors)
