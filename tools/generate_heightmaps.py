"""Generate the 8 x 8 terrain tiles used by KG5.1. Requires Python 3 only."""

import math
import struct
from pathlib import Path

TILE_COUNT = 8
TILE_CELLS = 32
WORLD_SIZE = 512
HEIGHT_RANGE = 190


def smoothstep(start, end, value):
    t = max(0.0, min(1.0, (value - start) / (end - start)))
    return t * t * (3 - 2 * t)


# Peak center, radii along X and Z, and height above the surrounding plateau.
PEAKS = (
    (-148, 83, 58, 68, 69),
    (147, 121, 64, 53, 72),
    (126, -91, 48, 51, 50),
    (-132, -125, 44, 59, 46),
)


def height(x, z):
    """A winding canyon with stepped walls, side gullies, and four peaks."""
    channel = x - (37 * math.sin(z / 72) + 14 * math.cos(z / 38))
    bank = abs(channel) + 6 * math.sin(z / 25 + x / 56)
    walls = (
        27 * smoothstep(23, 47, bank)
        + 31 * smoothstep(48, 77, bank)
        + 30 * smoothstep(78, 123, bank)
    )
    peaks = sum(amplitude * math.exp(-((x - cx) / rx) ** 2 - ((z - cz) / rz) ** 2)
                for cx, cz, rx, rz, amplitude in PEAKS)
    highland = smoothstep(30, 105, bank)
    gullies = highland * (10 * math.sin(z / 24 + x / 51) + 6 * math.sin(x / 22 - z / 37))
    detail = (1.5 + 2.5 * highland) * math.sin(x / 9 + z / 13) * math.cos(z / 11 - x / 17)
    bed = 13 + 4 * math.cos(z / 49) - 8 * math.exp(-(channel / 13) ** 2)
    return max(0, min(HEIGHT_RANGE, bed + walls + peaks + gullies + detail))


def main():
    output = Path(__file__).resolve().parents[1] / "KG5.1" / "Assets" / "Terrain"
    output.mkdir(parents=True, exist_ok=True)
    cells = TILE_COUNT * TILE_CELLS
    step = WORLD_SIZE / cells
    # Sample once globally so adjacent tiles share exactly the same border heights.
    samples = [
        [round(height(x * step - WORLD_SIZE / 2, z * step - WORLD_SIZE / 2)
               * 65535 / HEIGHT_RANGE) for x in range(cells + 1)]
        for z in range(cells + 1)
    ]
    side = TILE_CELLS + 1
    header = f"P5\n{side} {side}\n65535\n".encode("ascii")
    for tile_z in range(TILE_COUNT):
        for tile_x in range(TILE_COUNT):
            x = tile_x * TILE_CELLS
            z = tile_z * TILE_CELLS
            tile = [h for row in samples[z:z + side] for h in row[x:x + side]]
            data = struct.pack(f">{len(tile)}H", *tile)
            (output / f"height_{tile_x}_{tile_z}.pgm").write_bytes(header + data)


if __name__ == "__main__":
    main()
