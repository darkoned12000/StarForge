#!/usr/bin/env python3
"""Validate generated GLB assets (winding, POSITION bounds, samplers, known names)."""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from assetgen.validate import validate_known_assets


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    out_dir = argv[0] if argv else "assets/models"
    errors, warnings = validate_known_assets(out_dir)
    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"error: {e}")
    if errors:
        print(f"FAILED: {len(errors)} error(s)")
        return 1
    print(f"OK: validated {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
