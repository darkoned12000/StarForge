#!/usr/bin/env python3
"""Generate detailed comic-inked glTF (.glb) ship and station assets for Starforge.

Pure Python standard library (deterministic output, zero external dependencies).
Usage: python3 tools/generate_assets.py [output_dir]
"""

import json
import math
import os
import struct
import sys
import zlib

GLB_MAGIC = 0x46546C67
GLB_VERSION = 2
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


def clamp_byte(v: float) -> int:
    return 0 if v <= 0.0 else (255 if v >= 255.0 else int(v))


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def make_png_rows(width: int, height: int, row_fn) -> bytes:
    """Fast scanline PNG encoder; `row_fn(y)` returns `width * 4` RGBA bytes."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        raw.extend(row_fn(y))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        + png_chunk(b"IEND", b"")
    )


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_norm(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length < 1e-8:
        return (0.0, 1.0, 0.0)
    return (v[0] / length, v[1] / length, v[2] / length)


class Mesh:
    def __init__(self):
        self.positions = []
        self.normals = []
        self.uvs = []
        self.indices = []

    def vertex(self, position, normal, uv) -> int:
        index = len(self.positions) // 3
        self.positions.extend(position)
        self.normals.extend(normal)
        self.uvs.extend(uv)
        return index

    def triangle(self, a: int, b: int, c: int):
        self.indices.extend((a, b, c))

    def quad(self, p0, p1, p2, p3, uv0=(0.0, 0.0), uv1=(1.0, 0.0),
             uv2=(1.0, 1.0), uv3=(0.0, 1.0), outward_hint=None):
        """Adds a flat-shaded quad with automatic outward CCW winding."""
        n = v_norm(v_cross(v_sub(p1, p0), v_sub(p2, p0)))
        if outward_hint is not None and v_dot(n, outward_hint) < 0.0:
            p1, p3 = p3, p1
            uv1, uv3 = uv3, uv1
            n = (-n[0], -n[1], -n[2])
        i0 = self.vertex(p0, n, uv0)
        i1 = self.vertex(p1, n, uv1)
        i2 = self.vertex(p2, n, uv2)
        i3 = self.vertex(p3, n, uv3)
        self.triangle(i0, i1, i2)
        self.triangle(i0, i2, i3)

    def box(self, center, size, uv_scale=(1.0, 1.0)):
        cx, cy, cz = center
        sx, sy, sz = (s * 0.5 for s in size)
        su, sv = uv_scale
        faces = [
            ((0, 0, 1),  [(-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz)]),
            ((0, 0, -1), [(sx, -sy, -sz), (-sx, -sy, -sz), (-sx, sy, -sz), (sx, sy, -sz)]),
            ((1, 0, 0),  [(sx, -sy, sz), (sx, -sy, -sz), (sx, sy, -sz), (sx, sy, sz)]),
            ((-1, 0, 0), [(-sx, -sy, -sz), (-sx, -sy, sz), (-sx, sy, sz), (-sx, sy, -sz)]),
            ((0, 1, 0),  [(-sx, sy, sz), (sx, sy, sz), (sx, sy, -sz), (-sx, sy, -sz)]),
            ((0, -1, 0), [(-sx, -sy, -sz), (sx, -sy, -sz), (sx, -sy, sz), (-sx, -sy, sz)]),
        ]
        for normal, corners in faces:
            pts = [(cx + p[0], cy + p[1], cz + p[2]) for p in corners]
            self.quad(
                pts[0], pts[1], pts[2], pts[3],
                (0.0, 0.0), (su, 0.0), (su, sv), (0.0, sv),
                outward_hint=normal,
            )

    def loft(self, rings, uv_u_scale=1.0, uv_v_scale=1.0, cap_start=True, cap_end=True):
        """Lofts a sequence of closed 3D polygon rings along Z with outward normals."""
        ring_count = len(rings)
        seg_count = len(rings[0])
        for r_idx in range(ring_count - 1):
            r0 = rings[r_idx]
            r1 = rings[r_idx + 1]
            v0 = (r_idx / (ring_count - 1)) * uv_v_scale
            v1 = ((r_idx + 1) / (ring_count - 1)) * uv_v_scale
            c0 = (
                sum(p[0] for p in r0) / seg_count,
                sum(p[1] for p in r0) / seg_count,
                sum(p[2] for p in r0) / seg_count,
            )
            for s in range(seg_count):
                sn = (s + 1) % seg_count
                u0 = (s / seg_count) * uv_u_scale
                u1 = ((s + 1) / seg_count) * uv_u_scale
                mid = (
                    (r0[s][0] + r0[sn][0] + r1[s][0] + r1[sn][0]) * 0.25,
                    (r0[s][1] + r0[sn][1] + r1[s][1] + r1[sn][1]) * 0.25,
                    (r0[s][2] + r0[sn][2] + r1[s][2] + r1[sn][2]) * 0.25,
                )
                outward = v_sub(mid, c0)
                self.quad(
                    r0[s], r0[sn], r1[sn], r1[s],
                    (u0, v0), (u1, v0), (u1, v1), (u0, v1),
                    outward_hint=outward,
                )

        if cap_start:
            r0 = rings[0]
            center = (
                sum(p[0] for p in r0) / seg_count,
                sum(p[1] for p in r0) / seg_count,
                sum(p[2] for p in r0) / seg_count,
            )
            next_r = rings[1]
            next_c = (
                sum(p[0] for p in next_r) / seg_count,
                sum(p[1] for p in next_r) / seg_count,
                sum(p[2] for p in next_r) / seg_count,
            )
            out_dir = v_norm(v_sub(center, next_c))
            for s in range(seg_count):
                sn = (s + 1) % seg_count
                n = v_norm(v_cross(v_sub(r0[s], center), v_sub(r0[sn], center)))
                a, b = (r0[s], r0[sn]) if v_dot(n, out_dir) >= 0.0 else (r0[sn], r0[s])
                ic = self.vertex(center, out_dir, (0.5, 0.5))
                ia = self.vertex(a, out_dir, (0.5 + 0.45 * math.cos(s), 0.5 + 0.45 * math.sin(s)))
                ib = self.vertex(b, out_dir, (0.5 + 0.45 * math.cos(sn), 0.5 + 0.45 * math.sin(sn)))
                self.triangle(ic, ia, ib)

        if cap_end:
            r_last = rings[-1]
            center = (
                sum(p[0] for p in r_last) / seg_count,
                sum(p[1] for p in r_last) / seg_count,
                sum(p[2] for p in r_last) / seg_count,
            )
            prev_r = rings[-2]
            prev_c = (
                sum(p[0] for p in prev_r) / seg_count,
                sum(p[1] for p in prev_r) / seg_count,
                sum(p[2] for p in prev_r) / seg_count,
            )
            out_dir = v_norm(v_sub(center, prev_c))
            for s in range(seg_count):
                sn = (s + 1) % seg_count
                n = v_norm(v_cross(v_sub(r_last[s], center), v_sub(r_last[sn], center)))
                a, b = (r_last[s], r_last[sn]) if v_dot(n, out_dir) >= 0.0 else (r_last[sn], r_last[s])
                ic = self.vertex(center, out_dir, (0.5, 0.5))
                ia = self.vertex(a, out_dir, (0.5 + 0.45 * math.cos(s), 0.5 + 0.45 * math.sin(s)))
                ib = self.vertex(b, out_dir, (0.5 + 0.45 * math.cos(sn), 0.5 + 0.45 * math.sin(sn)))
                self.triangle(ic, ia, ib)

    def cylinder_z(self, cx, cy, z0, z1, r0, r1, segments=16, cap_start=True, cap_end=True, uv_u=2.0, uv_v=1.0):
        rings = []
        for z, r in ((z0, r0), (z1, r1)):
            ring = []
            for i in range(segments):
                ang = (i / segments) * 2.0 * math.pi
                ring.append((cx + r * math.cos(ang), cy + r * math.sin(ang), z))
            rings.append(ring)
        self.loft(rings, uv_u_scale=uv_u, uv_v_scale=uv_v, cap_start=cap_start, cap_end=cap_end)

    def torus_ring_xy(self, major_r, tube_rx, tube_rz, major_segs=48, tube_sides=8, uv_u=12.0):
        """Builds a solid 3D hexagonal/octagonal habitat ring in the XY plane."""
        tube_angles = [((j + 0.5) / tube_sides) * 2.0 * math.pi for j in range(tube_sides)]
        for i in range(major_segs):
            a0 = (i / major_segs) * 2.0 * math.pi
            a1 = ((i + 1) / major_segs) * 2.0 * math.pi
            u0 = (i / major_segs) * uv_u
            u1 = ((i + 1) / major_segs) * uv_u
            c0 = (major_r * math.cos(a0), major_r * math.sin(a0), 0.0)
            c1 = (major_r * math.cos(a1), major_r * math.sin(a1), 0.0)
            for j in range(tube_sides):
                jn = (j + 1) % tube_sides
                t0 = tube_angles[j]
                t1 = tube_angles[jn]
                v0 = j / tube_sides
                v1 = (j + 1) / tube_sides

                r_a0 = major_r + tube_rx * math.cos(t0)
                r_b0 = major_r + tube_rx * math.cos(t1)
                z_a = tube_rz * math.sin(t0)
                z_b = tube_rz * math.sin(t1)

                p00 = (r_a0 * math.cos(a0), r_a0 * math.sin(a0), z_a)
                p10 = (r_a0 * math.cos(a1), r_a0 * math.sin(a1), z_a)
                p11 = (r_b0 * math.cos(a1), r_b0 * math.sin(a1), z_b)
                p01 = (r_b0 * math.cos(a0), r_b0 * math.sin(a0), z_b)

                mid = (
                    (p00[0] + p10[0] + p11[0] + p01[0]) * 0.25,
                    (p00[1] + p10[1] + p11[1] + p01[1]) * 0.25,
                    (p00[2] + p10[2] + p11[2] + p01[2]) * 0.25,
                )
                c_mid = ((c0[0] + c1[0]) * 0.5, (c0[1] + c1[1]) * 0.5, 0.0)
                self.quad(
                    p00, p10, p11, p01,
                    (u0, v0), (u1, v0), (u1, v1), (u0, v1),
                    outward_hint=v_sub(mid, c_mid),
                )


class GlbBuilder:
    def __init__(self):
        self.blob = bytearray()
        self.buffer_views = []
        self.accessors = []
        self.images = []
        self.meshes = []
        self.materials = []

    def _view(self, data: bytes, target=None) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)
        offset = len(self.blob)
        self.blob.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.buffer_views.append(view)
        return len(self.buffer_views) - 1

    def float_accessor(self, values, components, target=34962) -> int:
        data = struct.pack("<%df" % len(values), *values)
        view = self._view(data, target)
        accessor = {
            "bufferView": view,
            "componentType": 5126,
            "count": len(values) // components,
            "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}[components],
        }
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def index_accessor(self, indices) -> int:
        # Use UNSIGNED_INT (5125) when vertex count exceeds 65535
        if max(indices, default=0) > 65535:
            data = struct.pack("<%dI" % len(indices), *indices)
            comp_type = 5125
        else:
            data = struct.pack("<%dH" % len(indices), *indices)
            comp_type = 5123
        view = self._view(data, 34963)
        self.accessors.append(
            {
                "bufferView": view,
                "componentType": comp_type,
                "count": len(indices),
                "type": "SCALAR",
            }
        )
        return len(self.accessors) - 1

    def image(self, png: bytes) -> int:
        view = self._view(png)
        self.images.append({"bufferView": view, "mimeType": "image/png"})
        return len(self.images) - 1

    def material(self, name, color, metallic, roughness, emissive=None, image=None) -> int:
        pbr = {
            "baseColorFactor": color,
            "metallicFactor": metallic,
            "roughnessFactor": roughness,
        }
        if image is not None:
            pbr["baseColorTexture"] = {"index": image}
        mat = {"name": name, "pbrMetallicRoughness": pbr, "doubleSided": False}
        if emissive:
            mat["emissiveFactor"] = emissive
        self.materials.append(mat)
        return len(self.materials) - 1

    def mesh(self, name, geometry: Mesh, material_index: int):
        position = self.float_accessor(geometry.positions, 3)
        normal = self.float_accessor(geometry.normals, 3)
        uv = self.float_accessor(geometry.uvs, 2)
        indices = self.index_accessor(geometry.indices)
        self.meshes.append(
            {
                "name": name,
                "primitives": [
                    {
                        "attributes": {
                            "POSITION": position,
                            "NORMAL": normal,
                            "TEXCOORD_0": uv,
                        },
                        "indices": indices,
                        "material": material_index,
                    }
                ],
            }
        )

    def build(self) -> bytes:
        while len(self.blob) % 4:
            self.blob.append(0)
        gltf = {
            "asset": {"version": "2.0", "generator": "starforge-generate-assets"},
            "scene": 0,
            "scenes": [{"nodes": list(range(len(self.meshes)))}],
            "nodes": [{"mesh": i, "name": "node-%d" % i} for i in range(len(self.meshes))],
            "buffers": [{"byteLength": len(self.blob)}],
            "bufferViews": self.buffer_views,
            "accessors": self.accessors,
            "images": self.images,
            "textures": [{"source": i} for i in range(len(self.images))],
            "materials": self.materials,
            "meshes": self.meshes,
        }
        json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
        json_bytes += b" " * ((-len(json_bytes)) % 4)
        total = 12 + 8 + len(json_bytes) + 8 + len(self.blob)
        out = bytearray()
        out.extend(struct.pack("<III", GLB_MAGIC, GLB_VERSION, total))
        out.extend(struct.pack("<II", len(json_bytes), JSON_CHUNK))
        out.extend(json_bytes)
        out.extend(struct.pack("<II", len(self.blob), BIN_CHUNK))
        out.extend(self.blob)
        return bytes(out)


# ---------------------------------------------------------------------------
# 256x256 Comic-Inked Procedural Textures
# ---------------------------------------------------------------------------

def make_inked_hull_png(size: int = 256) -> bytes:
    """Crisp off-white/slate armor plates with comic ink seams, AO grime, and rivets."""
    def row_fn(y: int) -> bytes:
        row = bytearray(size * 4)
        gy = y % 64
        by = y // 64
        for x in range(size):
            gx = x % 64
            bx = x // 64
            # Stagger every other row of armor plates like real hull plating
            sx = (x + (32 if by % 2 == 1 else 0)) % 64

            # Ink seam line (2px wide) + soft AO grime bordering the seam
            dist_edge = min(sx, 63 - sx, gy, 63 - gy)
            sub_seam = (gy == 32 and (bx + by) % 2 == 0)

            if dist_edge == 0 or sub_seam:
                r, g, b = 18, 22, 32
            elif dist_edge == 1:
                r, g, b = 95, 104, 118
            else:
                # Subtle panel-to-panel tonal variation + subtle weathering
                panel_tone = ((bx * 7 + by * 13) % 5) * 4
                ao = min(22, dist_edge * 2)
                base = 158 + panel_tone + (ao - 14)
                # Corner rivets
                if (sx in (5, 58)) and (gy in (5, 58)):
                    base -= 55
                r = clamp_byte(base - 4)
                g = clamp_byte(base)
                b = clamp_byte(base + 10)

            idx = x * 4
            row[idx] = r
            row[idx + 1] = g
            row[idx + 2] = b
            row[idx + 3] = 255
        return bytes(row)

    return make_png_rows(size, size, row_fn)


def make_inked_wing_png(size: int = 256) -> bytes:
    """Armored wing plating with comic ink lines and crimson squadron stripes."""
    def row_fn(y: int) -> bytes:
        row = bytearray(size * 4)
        v = y / (size - 1)
        gy = y % 64
        for x in range(size):
            u = x / (size - 1)
            gx = x % 64
            dist_edge = min(gx, 63 - gx, gy, 63 - gy)

            # Outer border ink + grid ink seams
            if x < 3 or x >= size - 3 or y < 3 or y >= size - 3 or dist_edge == 0:
                r, g, b = 18, 22, 32
            elif dist_edge == 1:
                r, g, b = 90, 98, 112
            else:
                # Red squadron stripe along outer wing span (u in 0.62..0.82)
                is_red_stripe = (0.62 <= u <= 0.82) and (0.10 <= v <= 0.90)
                stripe_border = (abs(u - 0.62) < 0.012 or abs(u - 0.82) < 0.012) and (0.10 <= v <= 0.90)
                if stripe_border:
                    r, g, b = 22, 24, 34
                elif is_red_stripe:
                    r, g, b = 205, 48, 44
                else:
                    tone = 154 + (((x // 64) + (y // 64)) % 3) * 6
                    r, g, b = tone - 4, tone, tone + 10

            idx = x * 4
            row[idx] = r
            row[idx + 1] = g
            row[idx + 2] = b
            row[idx + 3] = 255
        return bytes(row)

    return make_png_rows(size, size, row_fn)


def make_canopy_png(size: int = 128) -> bytes:
    """Tinted cockpit glass with crisp frame ink and diagonal specular reflection."""
    def row_fn(y: int) -> bytes:
        row = bytearray(size * 4)
        v = y / (size - 1)
        for x in range(size):
            u = x / (size - 1)
            # Canopy frame bars
            if u < 0.05 or u > 0.95 or v < 0.05 or v > 0.95 or abs(v - 0.52) < 0.025:
                r, g, b = 20, 26, 36
            else:
                diag = abs((u * 0.8 + v * 0.6) - 0.55)
                glint = 75 if diag < 0.045 else (32 if diag < 0.11 else 0)
                r = clamp_byte(24 + glint * 0.6)
                g = clamp_byte(75 + glint * 0.9)
                b = clamp_byte(130 + glint)
            idx = x * 4
            row[idx] = r
            row[idx + 1] = g
            row[idx + 2] = b
            row[idx + 3] = 255
        return bytes(row)

    return make_png_rows(size, size, row_fn)


def make_thruster_core_png(size: int = 128) -> bytes:
    """Hot white-cyan plasma core fading radially to deep electric blue."""
    half = (size - 1) * 0.5
    def row_fn(y: int) -> bytes:
        row = bytearray(size * 4)
        dy = (y - half) / half
        for x in range(size):
            dx = (x - half) / half
            dist = min(1.0, math.sqrt(dx * dx + dy * dy))
            core = (1.0 - dist) ** 1.6
            r = clamp_byte(30 + 225 * (core ** 2.2))
            g = clamp_byte(140 + 115 * core)
            b = 255
            idx = x * 4
            row[idx] = r
            row[idx + 1] = g
            row[idx + 2] = b
            row[idx + 3] = 255
        return bytes(row)

    return make_png_rows(size, size, row_fn)


def make_station_windows_png(size: int = 256) -> bytes:
    """Dark armored ring plating punctuated by glowing deck window bands."""
    def row_fn(y: int) -> bytes:
        row = bytearray(size * 4)
        gy = y % 64
        for x in range(size):
            gx = x % 32
            if gy in (0, 63) or gx == 0:
                r, g, b = 16, 20, 28
            elif (18 <= gy <= 24 or 40 <= gy <= 46) and (4 <= gx <= 26) and (gx % 6 != 0):
                # Warm amber/gold & cyan habitat windows
                if ((x // 32) + (y // 64)) % 4 == 0:
                    r, g, b = 120, 225, 255
                else:
                    r, g, b = 255, 195, 85
            else:
                r, g, b = 42, 48, 58
            idx = x * 4
            row[idx] = r
            row[idx + 1] = g
            row[idx + 2] = b
            row[idx + 3] = 255
        return bytes(row)

    return make_png_rows(size, size, row_fn)


# ---------------------------------------------------------------------------
# Hero Starfighter Geometry Builder (Nose = -Z, Wings = +-X, Up = +Y)
# ---------------------------------------------------------------------------

def build_swept_wing(mesh: Mesh, side: float):
    """Builds a beveled, swept-back delta wing for side = -1.0 (left) or +1.0 (right)."""
    stations = [
        (side * 0.52, -0.45, 1.55,  0.06, -0.09),
        (side * 1.55,  0.22, 1.68,  0.03, -0.07),
        (side * 2.52,  0.92, 1.76, -0.01, -0.06),
    ]
    for i in range(len(stations) - 1):
        x0, zf0, zb0, yt0, yb0 = stations[i]
        x1, zf1, zb1, yt1, yb1 = stations[i + 1]
        u0 = i / (len(stations) - 1)
        u1 = (i + 1) / (len(stations) - 1)

        mesh.quad(
            (x0, yt0, zf0), (x1, yt1, zf1), (x1, yt1, zb1), (x0, yt0, zb0),
            (u0, 0.0), (u1, 0.0), (u1, 1.0), (u0, 1.0),
            outward_hint=(0.0, 1.0, 0.0),
        )
        mesh.quad(
            (x0, yb0, zf0), (x1, yb1, zf1), (x1, yb1, zb1), (x0, yb0, zb0),
            (u0, 0.0), (u1, 0.0), (u1, 1.0), (u0, 1.0),
            outward_hint=(0.0, -1.0, 0.0),
        )
        mesh.quad(
            (x0, yb0, zf0), (x1, yb1, zf1), (x1, yt1, zf1), (x0, yt0, zf0),
            (u0, 0.0), (u1, 0.0), (u1, 0.15), (u0, 0.15),
            outward_hint=(0.0, 0.0, -1.0),
        )
        mesh.quad(
            (x0, yb0, zb0), (x1, yb1, zb1), (x1, yt1, zb1), (x0, yt0, zb0),
            (u0, 0.85), (u1, 0.85), (u1, 1.0), (u0, 1.0),
            outward_hint=(0.0, 0.0, 1.0),
        )

    xt, zft, zbt, ytt, ybt = stations[-1]
    mesh.quad(
        (xt, ybt, zft), (xt, ybt, zbt), (xt, ytt, zbt), (xt, ytt, zft),
        (0.95, 0.0), (1.0, 0.0), (1.0, 1.0), (0.95, 1.0),
        outward_hint=(side, 0.0, 0.0),
    )


def build_ship() -> bytes:
    glb = GlbBuilder()

    hull_img = glb.image(make_inked_hull_png(256))
    wing_img = glb.image(make_inked_wing_png(256))
    canopy_img = glb.image(make_canopy_png(128))
    core_img = glb.image(make_thruster_core_png(128))

    hull_mat = glb.material("hull-armor", [0.58, 0.63, 0.72, 1.0], 0.55, 0.35, image=hull_img)
    wing_mat = glb.material("wing-trim", [0.55, 0.60, 0.70, 1.0], 0.45, 0.42, image=wing_img)
    canopy_mat = glb.material("canopy-glass", [0.22, 0.55, 0.85, 1.0], 0.15, 0.18, image=canopy_img)
    housing_mat = glb.material("engine-housing", [0.26, 0.29, 0.36, 1.0], 0.75, 0.38, image=hull_img)
    core_mat = glb.material(
        "engine-core", [0.45, 0.92, 1.0, 1.0], 0.0, 0.9,
        emissive=[0.35, 0.88, 1.0], image=core_img
    )
    nav_mat = glb.material(
        "nav-lights", [1.0, 0.25, 0.15, 1.0], 0.0, 0.8,
        emissive=[1.0, 0.22, 0.10]
    )

    hull = Mesh()
    def octagon_section(z, rx, y_top, y_bot, bevel=0.28):
        bx = rx * (1.0 - bevel)
        ty = y_top * (1.0 - bevel * 0.7)
        by = y_bot * (1.0 - bevel * 0.7)
        return [
            (-bx,  y_top, z),
            ( bx,  y_top, z),
            ( rx,  ty,    z),
            ( rx,  by,    z),
            ( bx,  y_bot, z),
            (-bx,  y_bot, z),
            (-rx,  by,    z),
            (-rx,  ty,    z),
        ]

    fuselage_rings = [
        octagon_section(-2.25, 0.07,  0.03, -0.04),
        octagon_section(-1.55, 0.28,  0.14, -0.13),
        octagon_section(-0.65, 0.54,  0.26, -0.22),
        octagon_section( 0.45, 0.68,  0.32, -0.26),
        octagon_section( 1.55, 0.64,  0.28, -0.24),
        octagon_section( 1.85, 0.52,  0.22, -0.20),
    ]
    hull.loft(fuselage_rings, uv_u_scale=2.0, uv_v_scale=3.0)

    hull.box((0.0, 0.34, 0.85), (0.24, 0.14, 1.45), uv_scale=(0.5, 1.5))
    fin_rings = [
        [(-0.04, 0.38, 0.55), (0.04, 0.38, 0.55), (0.04, 0.44, 1.55), (-0.04, 0.44, 1.55)],
        [(-0.02, 0.86, 1.25), (0.02, 0.86, 1.25), (0.02, 0.88, 1.68), (-0.02, 0.88, 1.68)],
    ]
    hull.loft(fin_rings, uv_u_scale=1.0, uv_v_scale=1.0)

    for side in (-1.0, 1.0):
        hull.box((side * 0.66, -0.02, 0.25), (0.28, 0.26, 1.35), uv_scale=(0.5, 1.5))
        hull.cylinder_z(side * 0.72, -0.04, -1.15, -0.35, 0.035, 0.045, segments=8)
    glb.mesh("hero-hull", hull, hull_mat)

    wings = Mesh()
    build_swept_wing(wings, -1.0)
    build_swept_wing(wings,  1.0)
    for side in (-1.0, 1.0):
        wings.box((side * 2.56, -0.03, 1.32), (0.14, 0.14, 1.05), uv_scale=(0.3, 1.0))
    glb.mesh("hero-wings", wings, wing_mat)

    canopy = Mesh()
    canopy_rings = [
        [(-0.14, 0.23, -1.05), (0.14, 0.23, -1.05), (0.18, 0.18, -1.05), (-0.18, 0.18, -1.05)],
        [(-0.22, 0.44, -0.45), (0.22, 0.44, -0.45), (0.30, 0.27, -0.45), (-0.30, 0.27, -0.45)],
        [(-0.20, 0.42,  0.15), (0.20, 0.42,  0.15), (0.28, 0.28,  0.15), (-0.28, 0.28,  0.15)],
        [(-0.12, 0.32,  0.48), (0.12, 0.32,  0.48), (0.20, 0.26,  0.48), (-0.20, 0.26,  0.48)],
    ]
    canopy.loft(canopy_rings, uv_u_scale=1.0, uv_v_scale=1.0)
    glb.mesh("hero-canopy", canopy, canopy_mat)

    housings = Mesh()
    for side in (-1.0, 1.0):
        cx = side * 0.42
        housings.cylinder_z(cx, 0.02, 1.35, 2.08, 0.34, 0.30, segments=16, cap_end=False)
        housings.cylinder_z(cx, 0.02, 1.88, 2.08, 0.23, 0.25, segments=16, cap_start=False, cap_end=False)
        housings.box((cx, 0.02, 1.72), (0.64, 0.56, 0.55), uv_scale=(0.8, 0.8))
    glb.mesh("hero-engine-housings", housings, housing_mat)

    cores = Mesh()
    for side in (-1.0, 1.0):
        cx = side * 0.42
        segs = 20
        r = 0.235
        z_core = 1.92
        center_v = cores.vertex((cx, 0.02, z_core), (0.0, 0.0, 1.0), (0.5, 0.5))
        ring_ids = []
        for i in range(segs):
            ang = (i / segs) * 2.0 * math.pi
            ca, sa = math.cos(ang), math.sin(ang)
            ring_ids.append(
                cores.vertex(
                    (cx + r * ca, 0.02 + r * sa, z_core),
                    (0.0, 0.0, 1.0),
                    (0.5 + 0.5 * ca, 0.5 + 0.5 * sa),
                )
            )
        for i in range(segs):
            cores.triangle(center_v, ring_ids[i], ring_ids[(i + 1) % segs])
    glb.mesh("hero-engine-cores", cores, core_mat)

    nav = Mesh()
    for side in (-1.0, 1.0):
        nav.box((side * 2.62, -0.02, 1.05), (0.05, 0.06, 0.16))
    nav.box((0.0, 0.89, 1.52), (0.04, 0.05, 0.12))
    glb.mesh("hero-nav-lights", nav, nav_mat)

    return glb.build()


# ---------------------------------------------------------------------------
# Orbital Space Station Geometry Builder
# ---------------------------------------------------------------------------

def build_station() -> bytes:
    glb = GlbBuilder()

    hull_img = glb.image(make_inked_hull_png(256))
    win_img = glb.image(make_station_windows_png(256))

    hull_mat = glb.material("station-hull", [0.55, 0.60, 0.70, 1.0], 0.60, 0.42, image=hull_img)
    gold_mat = glb.material("station-trim", [0.72, 0.58, 0.28, 1.0], 0.78, 0.28, image=hull_img)
    win_mat = glb.material(
        "station-windows", [0.95, 0.88, 0.72, 1.0], 0.2, 0.5,
        emissive=[0.55, 0.45, 0.28], image=win_img
    )
    beacon_mat = glb.material(
        "station-beacons", [0.35, 0.88, 1.0, 1.0], 0.0, 0.9,
        emissive=[0.30, 0.82, 1.0]
    )

    hub = Mesh()
    hub.cylinder_z(0.0, 0.0, -1.10, 1.10, 0.95, 0.95, segments=12, uv_u=3.0, uv_v=2.0)
    hub.cylinder_z(0.0, 0.0,  1.10, 2.05, 0.62, 0.48, segments=12, uv_u=2.0, uv_v=1.5)
    hub.cylinder_z(0.0, 0.0,  2.05, 2.65, 0.36, 0.28, segments=10, uv_u=2.0, uv_v=1.0)
    hub.cylinder_z(0.0, 0.0,  2.65, 3.85, 0.06, 0.02, segments=8,  uv_u=1.0, uv_v=1.0)
    hub.cylinder_z(0.0, 0.0, -1.85, -1.10, 0.68, 0.78, segments=12, uv_u=2.0, uv_v=1.0)
    hub.cylinder_z(0.0, 0.0, -3.10, -1.85, 0.05, 0.14, segments=8,  uv_u=1.0, uv_v=1.0)

    for i in range(6):
        ang = i * (math.pi / 3.0)
        ca, sa = math.cos(ang), math.sin(ang)
        for step_r in (1.45, 2.35, 3.10):
            hub.box((step_r * ca, step_r * sa, 0.0), (0.85, 0.24, 0.24), uv_scale=(1.0, 0.4))
        perp_x, perp_y = -sa * 0.14, ca * 0.14
        for offset in (-1.0, 1.0):
            hub.box(
                (2.15 * ca + perp_x * offset, 2.15 * sa + perp_y * offset, 0.14),
                (2.35, 0.08, 0.08),
            )
            hub.box(
                (2.15 * ca + perp_x * offset, 2.15 * sa + perp_y * offset, -0.14),
                (2.35, 0.08, 0.08),
            )
        pod_r = 3.68
        hub.box((pod_r * ca, pod_r * sa, 0.0), (0.58, 0.52, 0.62), uv_scale=(1.0, 1.0))

    glb.mesh("station-hub", hub, hull_mat)

    rings = Mesh()
    rings.torus_ring_xy(major_r=2.55, tube_rx=0.22, tube_rz=0.24, major_segs=48, tube_sides=8, uv_u=16.0)
    rings.torus_ring_xy(major_r=3.35, tube_rx=0.18, tube_rz=0.20, major_segs=56, tube_sides=8, uv_u=20.0)
    glb.mesh("station-rings", rings, win_mat)

    trim = Mesh()
    for i in range(6):
        ang = i * (math.pi / 3.0) + (math.pi / 6.0)
        ca, sa = math.cos(ang), math.sin(ang)
        trim.box((2.55 * ca, 2.55 * sa, 0.36), (0.42, 0.42, 0.28), uv_scale=(0.8, 0.5))
        trim.box((2.55 * ca, 2.55 * sa, -0.36), (0.42, 0.42, 0.28), uv_scale=(0.8, 0.5))
    trim.torus_ring_xy(major_r=0.72, tube_rx=0.07, tube_rz=0.07, major_segs=24, tube_sides=6, uv_u=6.0)
    glb.mesh("station-trim", trim, gold_mat)

    beacons = Mesh()
    beacons.box((0.0, 0.0, 3.88), (0.12, 0.12, 0.12))
    beacons.box((0.0, 0.0, -3.14), (0.10, 0.10, 0.10))
    for i in range(6):
        ang = i * (math.pi / 3.0)
        beacons.box((3.98 * math.cos(ang), 3.98 * math.sin(ang), 0.0), (0.08, 0.14, 0.14))
    glb.mesh("station-beacons", beacons, beacon_mat)

    return glb.build()


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "assets/models"
    os.makedirs(out_dir, exist_ok=True)
    for name, data in (("hero_ship.glb", build_ship()), ("station.glb", build_station())):
        path = os.path.join(out_dir, name)
        with open(path, "wb") as file:
            file.write(data)
        print("wrote %s (%d bytes)" % (path, len(data)))


if __name__ == "__main__":
    main()
