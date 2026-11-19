#!/usr/bin/env python3
"""M17.7: generate the authored glTF assets used by the First Flight demo.

Everything here is deterministic so the committed .glb files can be
reproduced byte-for-byte. The meshes are deliberately simple but read as a
ship and a station in the toon renderer; materials and embedded textures
exercise the glTF -> toon mapping and texture streaming paths.

Usage: python3 tools/generate_assets.py [output_dir]
"""

import json
import os
import struct
import sys
import zlib

GLB_MAGIC = 0x46546C67
GLB_VERSION = 2
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def make_png(width: int, height: int, pixel) -> bytes:
    """Minimal RGBA8 PNG encoder; `pixel(x, y)` returns (r, g, b, a)."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        for x in range(width):
            raw.extend(pixel(x, y))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) +
            png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + png_chunk(b"IEND", b""))


class Mesh:
    def __init__(self):
        self.positions = []
        self.normals = []
        self.uvs = []
        self.indices = []

    def vertex(self, position, normal, uv):
        index = len(self.positions) // 3
        self.positions.extend(position)
        self.normals.extend(normal)
        self.uvs.extend(uv)
        return index

    def triangle(self, a, b, c):
        self.indices.extend((a, b, c))

    def box(self, center, size):
        cx, cy, cz = center
        sx, sy, sz = (s * 0.5 for s in size)
        faces = [
            ((0, 0, 1), [(-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz)]),
            ((0, 0, -1), [(sx, -sy, -sz), (-sx, -sy, -sz), (-sx, sy, -sz), (sx, sy, -sz)]),
            ((1, 0, 0), [(sx, -sy, sz), (sx, -sy, -sz), (sx, sy, -sz), (sx, sy, sz)]),
            ((-1, 0, 0), [(-sx, -sy, -sz), (-sx, -sy, sz), (-sx, sy, sz), (-sx, sy, -sz)]),
            ((0, 1, 0), [(-sx, sy, sz), (sx, sy, sz), (sx, sy, -sz), (-sx, sy, -sz)]),
            ((0, -1, 0), [(-sx, -sy, -sz), (sx, -sy, -sz), (sx, -sy, sz), (-sx, -sy, sz)]),
        ]
        for normal, corners in faces:
            ids = [self.vertex((cx + p[0], cy + p[1], cz + p[2]), normal,
                               (i & 1, i >> 1)) for i, p in enumerate(corners)]
            self.triangle(ids[0], ids[1], ids[2])
            self.triangle(ids[0], ids[2], ids[3])

    def wedge(self, length, width, height):
        """A tapered forward hull: wide at the back, sharp at the nose."""
        hx, hy, hz = width * 0.5, height * 0.5, length * 0.5
        back = [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]
        nose = (0, 0, -hz)
        ids = [self.vertex(p, (0, 0, 1), (i & 1, i >> 1)) for i, p in enumerate(back)]
        tip = self.vertex(nose, (0, 0, -1), (0.5, 0.5))
        for i in range(4):
            j = (i + 1) % 4
            self.triangle(ids[i], ids[j], tip)

    def ring(self, radius, thickness, segments=16):
        for i in range(segments):
            a0 = i * 2 * 3.14159265 / segments
            a1 = (i + 1) * 2 * 3.14159265 / segments
            p0 = (radius * __import__("math").cos(a0), radius * __import__("math").sin(a0), 0)
            p1 = (radius * __import__("math").cos(a1), radius * __import__("math").sin(a1), 0)
            n = (0, 0, 1)
            a = self.vertex((p0[0], p0[1], thickness), n, (i / segments, 1))
            b = self.vertex((p1[0], p1[1], thickness), n, ((i + 1) / segments, 1))
            c = self.vertex((p1[0], p1[1], -thickness), n, ((i + 1) / segments, 0))
            d = self.vertex((p0[0], p0[1], -thickness), n, (i / segments, 0))
            self.triangle(a, b, c)
            self.triangle(a, c, d)


class GlbBuilder:
    def __init__(self):
        self.blob = bytearray()
        self.buffer_views = []
        self.accessors = []
        self.images = []
        self.meshes = []
        self.materials = []

    def _view(self, data: bytes, target=None):
        while len(self.blob) % 4:
            self.blob.append(0)
        offset = len(self.blob)
        self.blob.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.buffer_views.append(view)
        return len(self.buffer_views) - 1

    def float_accessor(self, values, components, target=34962):
        data = struct.pack("<%df" % len(values), *values)
        view = self._view(data, target)
        accessor = {"bufferView": view, "componentType": 5126,
                    "count": len(values) // components,
                    "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}[components]}
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def index_accessor(self, indices):
        data = struct.pack("<%dH" % len(indices), *indices)
        view = self._view(data, 34963)
        self.accessors.append({"bufferView": view, "componentType": 5123,
                               "count": len(indices), "type": "SCALAR"})
        return len(self.accessors) - 1

    def image(self, png: bytes):
        view = self._view(png)
        self.images.append({"bufferView": view, "mimeType": "image/png"})
        return len(self.images) - 1

    def material(self, name, color, metallic, roughness, emissive=None, image=None):
        pbr = {"baseColorFactor": color, "metallicFactor": metallic,
               "roughnessFactor": roughness}
        if image is not None:
            pbr["baseColorTexture"] = {"index": image}
        material = {"name": name, "pbrMetallicRoughness": pbr, "doubleSided": False}
        if emissive:
            material["emissiveFactor"] = emissive
        self.materials.append(material)
        return len(self.materials) - 1

    def mesh(self, name, geometry, material_index):
        position = self.float_accessor(geometry.positions, 3)
        normal = self.float_accessor(geometry.normals, 3)
        uv = self.float_accessor(geometry.uvs, 2)
        indices = self.index_accessor(geometry.indices)
        self.meshes.append({"name": name, "primitives": [{
            "attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv},
            "indices": indices, "material": material_index}]})

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


def hull_texture(x, y):
    band = 90 if (x // 2 + y // 2) % 2 == 0 else 130
    return (band, band + 10, band + 20, 255)


def wing_texture(x, y):
    return (60 + x * 18, 80 + y * 16, 110, 255)


def engine_texture(x, y):
    return (255, 180 + x * 6, 60 + y * 12, 255)


def station_texture(x, y):
    shade = 110 if (x + y) % 3 else 160
    return (shade, shade + 12, shade + 24, 255)


def build_ship() -> bytes:
    glb = GlbBuilder()
    hull_img = glb.image(make_png(8, 8, hull_texture))
    wing_img = glb.image(make_png(8, 8, wing_texture))
    engine_img = glb.image(make_png(8, 8, engine_texture))

    hull_mat = glb.material("hull", [0.85, 0.88, 0.95, 1.0], 0.7, 0.35, image=hull_img)
    wing_mat = glb.material("wings", [0.35, 0.55, 0.85, 1.0], 0.2, 0.6, image=wing_img)
    engine_mat = glb.material("engines", [1.0, 0.8, 0.4, 1.0], 0.0, 0.9,
                              emissive=[0.9, 0.45, 0.1], image=engine_img)

    hull = Mesh()
    hull.wedge(4.0, 1.6, 0.8)
    glb.mesh("hero-hull", hull, hull_mat)

    wings = Mesh()
    wings.box((-1.5, -0.1, 0.4), (2.2, 0.12, 1.4))
    wings.box((1.5, -0.1, 0.4), (2.2, 0.12, 1.4))
    glb.mesh("hero-wings", wings, wing_mat)

    engines = Mesh()
    engines.box((-0.55, 0.0, 2.1), (0.45, 0.45, 0.5))
    engines.box((0.55, 0.0, 2.1), (0.45, 0.45, 0.5))
    glb.mesh("hero-engines", engines, engine_mat)
    return glb.build()


def build_station() -> bytes:
    glb = GlbBuilder()
    station_img = glb.image(make_png(8, 8, station_texture))
    hull_mat = glb.material("station-hull", [0.7, 0.75, 0.85, 1.0], 0.5, 0.5, image=station_img)
    ring_mat = glb.material("station-ring", [0.95, 0.75, 0.3, 1.0], 0.8, 0.25, image=station_img)

    hub = Mesh()
    hub.box((0, 0, 0), (2.0, 1.2, 2.0))
    glb.mesh("station-hub", hub, hull_mat)

    ring = Mesh()
    ring.ring(2.6, 0.18, 24)
    glb.mesh("station-ring", ring, ring_mat)

    arms = Mesh()
    for angle in (0, 2.094, 4.189):
        import math
        cx, cy = 1.4 * math.cos(angle), 1.4 * math.sin(angle)
        arms.box((cx, cy, 0), (2.0, 0.2, 0.2))
    glb.mesh("station-arms", arms, hull_mat)
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
