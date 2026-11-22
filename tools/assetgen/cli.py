"""CLI entry point for procedural asset generation."""
from __future__ import annotations

import os
import sys

from .kitbash import build_ship_kitbash
from .ship import build_ship
from .station import build_station


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    out_dir = argv[0] if len(argv) > 0 else "assets/models"
    tex_size = int(argv[1]) if len(argv) > 1 else 512
    os.makedirs(out_dir, exist_ok=True)
    outputs = [
        ("hero_ship.glb", build_ship(tex_size)),
        ("station.glb", build_station(tex_size)),
        ("hero_ship_kitbash.glb", build_ship_kitbash(tex_size)),
    ]
    for name, data in outputs:
        path = os.path.join(out_dir, name)
        with open(path, "wb") as file:
            file.write(data)
        print("wrote %s (%d bytes)" % (path, len(data)))


if __name__ == "__main__":
    main()
