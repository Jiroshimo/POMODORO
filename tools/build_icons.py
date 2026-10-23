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
    frames.append((size, bytes(png)))

# PNG-compressed ICO frames preserve transparency and sharp pixel edges.
offset = 6 + 16 * len(frames)
directory = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
for size, png in frames:
    directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset))
    offset += len(png)
(assets / "stash.ico").write_bytes(directory + b"".join(png for _, png in frames))
(assets / "stash.png").write_bytes(frames[-1][1])
