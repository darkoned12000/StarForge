#!/usr/bin/env python3
"""Smoke/validation tests for tools/generate_assets.py."""
from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from assetgen.validate import validate_glb, validate_known_assets  # noqa: E402


def read_glb(path):
    data = Path(path).read_bytes()
    json_len = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20 : 20 + json_len])
    return data, gltf


def test_generator_runs_and_validates():
    with tempfile.TemporaryDirectory() as td:
        cmd = [sys.executable, str(ROOT / "tools/generate_assets.py"), td, "256"]
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
        errors, warnings = validate_known_assets(td)
        assert not errors, f"errors: {errors}\nwarnings: {warnings}\n{r.stdout}"
        for name in ("hero_ship.glb", "station.glb"):
            _, gltf = read_glb(Path(td) / name)
            assert gltf.get("samplers"), f"{name}: missing samplers"
            for tex in gltf.get("textures", []):
                assert "sampler" in tex, f"{name}: texture missing sampler"
            for mesh in gltf.get("meshes", []):
                for prim in mesh.get("primitives", []):
                    pos_acc = gltf["accessors"][prim["attributes"]["POSITION"]]
                    assert "min" in pos_acc and "max" in pos_acc, f"{name}: POSITION missing min/max"


def test_known_asset_contract():
    # Generated assets in repo must keep game/test mesh contract.
    errors, warnings = validate_known_assets(ROOT / "assets/models")
    assert not errors, f"errors: {errors}\nwarnings: {warnings}"
    _, ship = read_glb(ROOT / "assets/models/hero_ship.glb")
    _, station = read_glb(ROOT / "assets/models/station.glb")
    assert [m["name"] for m in ship["meshes"]] == [
        "hero-hull", "hero-wings", "hero-canopy",
        "hero-engine-housings", "hero-engine-cores", "hero-nav-lights",
    ]
    assert [m["name"] for m in station["meshes"]] == [
        "station-hub", "station-rings", "station-trim", "station-beacons",
    ]


def main():
    tests = [test_generator_runs_and_validates, test_known_asset_contract]
    for t in tests:
        t()
        print(f"ok: {t.__name__}")
    print(f"PASSED {len(tests)} tests")


if __name__ == "__main__":
    main()
