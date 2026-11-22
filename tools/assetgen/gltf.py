"""Minimal GLB/glTF 2.0 writer for generated assets."""
from __future__ import annotations

import json
import struct

GLB_MAGIC = 0x46546C67
GLB_VERSION = 2
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


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

    def float_accessor(self, values, components, target=34962, bounds=False) -> int:
        data = struct.pack("<%df" % len(values), *values)
        view = self._view(data, target)
        accessor = {
            "bufferView": view,
            "componentType": 5126,
            "count": len(values) // components,
            "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}[components],
        }
        if bounds and values:
            cols = [values[i::components] for i in range(components)]
            accessor["min"] = [min(c) for c in cols]
            accessor["max"] = [max(c) for c in cols]
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

    def material(self, name, color, metallic, roughness, emissive=None, image=None,
                 emissive_image=None, emissive_factor=None) -> int:
        pbr = {
            "baseColorFactor": color,
            "metallicFactor": metallic,
            "roughnessFactor": roughness,
        }
        if image is not None:
            pbr["baseColorTexture"] = {"index": image}
        mat = {"name": name, "pbrMetallicRoughness": pbr, "doubleSided": False}
        if emissive_factor is not None:
            mat["emissiveFactor"] = emissive_factor
        elif emissive:
            mat["emissiveFactor"] = emissive
        if emissive_image is not None:
            mat["emissiveTexture"] = {"index": emissive_image}
        self.materials.append(mat)
        return len(self.materials) - 1

    def mesh(self, name, geometry: Mesh, material_index: int):
        position = self.float_accessor(geometry.positions, 3, bounds=True)
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
            "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
            "textures": [{"sampler": 0, "source": i} for i in range(len(self.images))],
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


