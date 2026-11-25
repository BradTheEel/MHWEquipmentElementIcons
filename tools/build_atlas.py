"""Build the private MHW atlas by copying the original BC7 blocks."""

import argparse
import hashlib
import json
import re
import struct
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
STOCK_SHA256 = "dc901307840f068310876902f2e4d68446bac3288115ee173dfd849b4db7a712"
STOCK_COLUMNS = 8
STOCK_ROWS = 16
STOCK_TILE_SIZE = 64
BC7_BLOCK_SIZE = 4
BC7_BLOCK_BYTES = 16


def build_atlas(stock, layout):
    if hashlib.sha256(stock).hexdigest() != STOCK_SHA256:
        raise ValueError("Expected the stock cmn_status00_ID.tex from Steam build 15539686.")
    if layout["tile_size"] != STOCK_TILE_SIZE:
        raise ValueError("Direct BC7 copying requires the stock tile size.")

    columns, rows = layout["columns"], layout["rows"]
    icons = layout["icons"]
    if any(type(value) is not int for value in (columns, rows, layout["tile_size"])):
        raise ValueError("Atlas columns, rows and tile_size must be integers.")
    if not (1 <= columns <= 511 and 1 <= rows <= 511 and 1 <= len(icons) <= min(columns * rows, 256)):
        raise ValueError("Invalid atlas dimensions or icon count.")
    if [icon["id"] for icon in icons] != list(range(1, len(icons) + 1)):
        raise ValueError("Atlas icons must be ordered by effect ID starting at 1.")
    source_tiles = [icon["stock_tile"] for icon in icons]
    if len(set(source_tiles)) != len(source_tiles) or any(
        not 0 <= tile < STOCK_COLUMNS * STOCK_ROWS for tile in source_tiles
    ):
        raise ValueError("Stock tile indices must be unique and within the source atlas.")

    # The pinned stock texture uses one BC7 mip level. Its first mip starts at 0xc0.
    data_offset, = struct.unpack_from("<Q", stock, 0xB8)
    source = stock[data_offset:]
    header = bytearray(stock[:data_offset])
    width, height = columns * STOCK_TILE_SIZE, rows * STOCK_TILE_SIZE
    struct.pack_into("<II", header, 0x18, width, height)

    blocks_per_tile = STOCK_TILE_SIZE // BC7_BLOCK_SIZE
    stock_stride = STOCK_COLUMNS * blocks_per_tile * BC7_BLOCK_BYTES
    atlas_stride = columns * blocks_per_tile * BC7_BLOCK_BYTES
    row_bytes = blocks_per_tile * BC7_BLOCK_BYTES
    # The final block in the stock atlas is transparent and uses valid BC7 encoding.
    pixels = bytearray(source[-BC7_BLOCK_BYTES:] * (width * height // BC7_BLOCK_SIZE**2))
    for slot, tile in enumerate(source_tiles):
        source_col, source_row = tile % STOCK_COLUMNS, tile // STOCK_COLUMNS
        atlas_col, atlas_row = slot % columns, slot // columns
        for row in range(blocks_per_tile):
            start = (source_row * blocks_per_tile + row) * stock_stride + source_col * row_bytes
            end = (atlas_row * blocks_per_tile + row) * atlas_stride + atlas_col * row_bytes
            pixels[end:end + row_bytes] = source[start:start + row_bytes]

    return header + pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock_tex", type=Path, help="Extracted stock cmn_status00_ID.tex")
    parser.add_argument("--output-root", type=Path, default=PROJECT / "build")
    parser.add_argument("--check", action="store_true", help="Verify that the existing output matches the layout")
    args = parser.parse_args()
    layout = json.loads((PROJECT / "atlas-layout.json").read_text(encoding="utf-8"))
    resource = layout["resource_path"]
    if len(resource) > 79 or not re.fullmatch(r"ui(?:/[a-zA-Z0-9_]+)+", resource):
        raise ValueError("Invalid atlas resource path.")
    data = build_atlas(args.stock_tex.read_bytes(), layout)

    output = args.output_root / "nativePC" / f"{resource}.tex"
    if args.check:
        if not output.is_file() or output.read_bytes() != data:
            parser.error(f"Atlas is missing or out of date: {output}")
        print(f"PASS atlas matches the current layout: {output}")
        return

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.NamedTemporaryFile(dir=output.parent, delete=False)
    staging = Path(temporary.name)
    try:
        with temporary:
            temporary.write(data)
        staging.replace(output)
    finally:
        staging.unlink(missing_ok=True)
    print(output)


if __name__ == "__main__":
    main()
