"""Generate the 320x160 RGB565 indexed map used by the CYD sketch.

Run from the repository root after installing Pillow:
    python HamSatTune_CYD/assets/generate_world_map.py
"""

from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "WorldMap3.jpg"
OUTPUT = HERE.parent / "world_map_data.h"
WIDTH, HEIGHT = 320, 160

image = Image.open(SOURCE).convert("RGB")
image = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
image = image.quantize(colors=64, method=Image.Quantize.MEDIANCUT)
palette = image.getpalette()
indices = image.tobytes()

colors = []
for index in range(64):
    red, green, blue = palette[index * 3:index * 3 + 3]
    colors.append(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3))

with OUTPUT.open("w", encoding="ascii") as output:
    output.write("// Generated from assets/WorldMap3.jpg by assets/generate_world_map.py.\n")
    output.write("#pragma once\n#include <Arduino.h>\n\n")
    output.write("constexpr uint16_t WORLD_MAP_PALETTE[64] = {\n")
    for offset in range(0, 64, 8):
        output.write("  " + ", ".join(f"0x{value:04X}" for value in colors[offset:offset + 8]) + ",\n")
    output.write("};\n\n")
    output.write("constexpr uint8_t WORLD_MAP_PIXELS[320 * 160] = {\n")
    for offset in range(0, len(indices), 32):
        output.write("  " + ", ".join(str(value) for value in indices[offset:offset + 32]) + ",\n")
    output.write("};\n")
