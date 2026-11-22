"""Validation helpers for generated GLB assets."""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path


def _read_accessor(gltf, blob, idx):
    acc = gltf["accessors"][idx]
    bv = gltf["bufferViews"][acc["bufferView"]]
    off = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[acc["type"]]
    fmt = {5126: "f", 5123: "H", 5125: "I", 5121: "B", 5120: "b", 5122: "h"}[acc["componentType"]]
    vals = struct.unpack_from("<" + str(acc["count"] * ncomp) + fmt, blob, off)
    if ncomp == 1:
        return list(vals)
    return [vals[i : i + ncomp] for i in range(0, len(vals), ncomp)]


def _vsub(a, b): return tuple(a[i] - b[i] for i in range(3))
def _vcross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def _vdot(a, b): return sum(a[i]*b[i] for i in range(3))
def _vlen(a): return math.sqrt(_vdot(a, a))


def validate_glb(path):
    path = Path(path)
    errors = []
    warnings = []
    data = path.read_bytes()
    if len(data) < 20 or struct.unpack_from("<I", data, 0)[0] != 0x46546C67:
        return ["not a GLB file"], []
    json_len = struct.unpack_from("<I", data, 12)[0]
    if struct.unpack_from("<I", data, 16)[0] != 0x4E4F534A:
        return ["missing JSON chunk"], []
    gltf = json.loads(data[20 : 20 + json_len])
    bin_off = 20 + json_len
    if bin_off + 8 > len(data):
        return ["missing BIN chunk header"], []
    bin_len = struct.unpack_from("<I", data, bin_off)[0]
    bin_type = struct.unpack_from("<I", data, bin_off + 4)[0]
    if bin_type != 0x004E4942:
        return ["missing BIN chunk"], []
    blob = data[bin_off + 8 : bin_off + 8 + bin_len]

    samplers = gltf.get("samplers", [])
    if not samplers:
        warnings.append("no samplers defined")
    for tex in gltf.get("textures", []):
        if "sampler" not in tex and samplers:
            warnings.append("texture missing sampler reference")

    for acc in gltf.get("accessors", []):
        if acc.get("type") == "VEC3" and acc.get("componentType") == 5126:
            # POSITION accessors should have min/max
            pass

    for mi, mesh in enumerate(gltf.get("meshes", [])):
        mesh_name = mesh.get("name", f"mesh[{mi}]")
        for pi, prim in enumerate(mesh.get("primitives", [])):
            attrs = prim.get("attributes", {})
            if "POSITION" not in attrs:
                errors.append(f"{mesh_name}[{pi}]: missing POSITION")
                continue
            pos_acc = gltf["accessors"][attrs["POSITION"]]
            if "min" not in pos_acc or "max" not in pos_acc:
                errors.append(f"{mesh_name}[{pi}]: POSITION accessor missing min/max")
            if "NORMAL" not in attrs:
                warnings.append(f"{mesh_name}[{pi}]: missing NORMAL")
            if "indices" not in prim:
                warnings.append(f"{mesh_name}[{pi}]: non-indexed")
                continue
            pos = _read_accessor(gltf, blob, attrs["POSITION"])
            nrm = _read_accessor(gltf, blob, attrs["NORMAL"]) if "NORMAL" in attrs else None
            idx = _read_accessor(gltf, blob, prim["indices"])
            for t in range(len(idx) // 3):
                i0, i1, i2 = idx[t * 3 : t * 3 + 3]
                if i0 >= len(pos) or i1 >= len(pos) or i2 >= len(pos):
                    errors.append(f"{mesh_name}[{pi}]: index out of range at tri {t}")
                    break
                if nrm is None:
                    continue
                a, b, c = pos[i0], pos[i1], pos[i2]
                fn = _vcross(_vsub(b, a), _vsub(c, a))
                avg = (nrm[i0][0]+nrm[i1][0]+nrm[i2][0], nrm[i0][1]+nrm[i1][1]+nrm[i2][1], nrm[i0][2]+nrm[i1][2]+nrm[i2][2])
                if _vlen(fn) > 1e-12 and _vlen(avg) > 1e-12 and _vdot(fn, avg) < 0:
                    errors.append(f"{mesh_name}[{pi}]: inward winding at tri {t}")
                    if len(errors) > 20:
                        errors.append("...too many errors")
                        return errors, warnings
    return errors, warnings


def validate_known_assets(out_dir):
    out_dir = Path(out_dir)
    errors = []
    warnings = []
    ship = out_dir / "hero_ship.glb"
    station = out_dir / "station.glb"
    for p in (ship, station):
        if not p.exists():
            errors.append(f"missing {p}")
            continue
        e, w = validate_glb(p)
        errors.extend([f"{p.name}: {x}" for x in e])
        warnings.extend([f"{p.name}: {x}" for x in w])
    if ship.exists():
        gltf = json.loads(ship.read_bytes()[20:20+struct.unpack_from('<I', ship.read_bytes(), 12)[0]])
        names = [m.get("name") for m in gltf.get("meshes", [])]
        expected = ["hero-hull","hero-wings","hero-canopy","hero-engine-housings","hero-engine-cores","hero-nav-lights"]
        if names != expected:
            errors.append(f"hero_ship.glb: unexpected mesh names {names}")
    if station.exists():
        gltf = json.loads(station.read_bytes()[20:20+struct.unpack_from('<I', station.read_bytes(), 12)[0]])
        names = [m.get("name") for m in gltf.get("meshes", [])]
        expected = ["station-hub","station-rings","station-trim","station-beacons"]
        if names != expected:
            errors.append(f"station.glb: unexpected mesh names {names}")
    return errors, warnings
