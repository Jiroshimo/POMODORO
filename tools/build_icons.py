#!/usr/bin/env python3
"""Export the black-and-white pixel hourglass for GTK and Windows (requires PyGObject)."""
from pathlib import Path
import struct

import gi

gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf

assets = Path(__file__).resolve().parent.parent / "assets"
source = GdkPixbuf.Pixbuf.new_from_file_at_scale(str(assets / "stash.svg"), 32, 32, True)
frames = []
for size in (16, 24, 32, 48, 64, 128, 256):
    scaled = source.scale_simple(size, size, GdkPixbuf.InterpType.NEAREST)
    success, png = scaled.save_to_bufferv("png", [], [])
    if not success:
        raise RuntimeError(f"Could not render {size}px icon")
    png = bytes(png)
    if size == 256:
        large_png = png
        frames.append((size, png))
        continue
    # Classic DIB frames work with Windows shell icons and System.Drawing.
    # Reserve PNG compression for the large 256px thumbnail.
    raw = scaled.get_pixels()
    channels = scaled.get_n_channels()
    stride = scaled.get_rowstride()
    pixels = bytearray()
    mask = bytearray()
    for y in reversed(range(size)):
        mask_row = bytearray(((size + 31) // 32) * 4)
        for x in range(size):
            index = y * stride + x * channels
            red, green, blue = raw[index:index + 3]
            alpha = raw[index + 3] if channels == 4 else 255
            pixels.extend((blue, green, red, alpha))
            if alpha == 0:
                mask_row[x // 8] |= 1 << (7 - x % 8)
        mask.extend(mask_row)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(pixels), 0, 0, 0, 0)
    frames.append((size, header + pixels + mask))

offset = 6 + 16 * len(frames)
directory = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
for size, frame in frames:
    directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(frame), offset))
    offset += len(frame)
(assets / "stash.ico").write_bytes(directory + b"".join(frame for _, frame in frames))
(assets / "stash.png").write_bytes(large_png)
