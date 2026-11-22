"""Data-driven kitbash helpers: mirrored parts and socketed assembly.

Parts are small reusable mesh builders; sockets describe where parts attach.
This is a foundation for a larger part library.
"""
from __future__ import annotations

from dataclasses import dataclass

from .gltf import GlbBuilder
from .mesh import Mesh, v_norm
from .textures import (
    make_canopy_png,
    make_inked_hull_png,
    make_inked_wing_png,
    make_nav_emissive_png,
    make_thruster_core_png,
    make_thruster_emissive_png,
)


@dataclass(frozen=True)
class Socket:
    name: str
    position: tuple[float, float, float]
    scale: float = 1.0
    mirror_x: bool = False


# Data-driven part library: part name -> builder(mesh, socket).
def part_hull_core(mesh: Mesh, s: Socket) -> None:
    x, y, z = s.position; k = s.scale
    mesh.box((x, y, z), (1.0 * k, 0.5 * k, 3.2 * k), tile=2.0)
    mesh.chamfered_box((x, y + 0.18 * k, z - 1.0 * k), (0.55 * k, 0.35 * k, 1.2 * k), bevel=0.06 * k, tile=2.0)
    mesh.chamfered_box((x, y - 0.05 * k, z + 1.3 * k), (0.8 * k, 0.45 * k, 1.1 * k), bevel=0.08 * k, tile=2.0)


def part_wing(mesh: Mesh, s: Socket) -> None:
    x, y, z = s.position; k = s.scale
    mesh.box((x, y, z), (1.7 * k, 0.08 * k, 1.3 * k), tile=2.0)
    mesh.chamfered_box((x + 0.9 * k, y + 0.01 * k, z + 0.4 * k), (0.35 * k, 0.12 * k, 0.8 * k), bevel=0.03 * k, tile=2.0)


def part_engine_pod(mesh: Mesh, s: Socket) -> None:
    cx, cy, cz = s.position; k = s.scale
    mesh.cylinder_z(cx, cy, cz, cz + 0.72 * k, 0.34 * k, 0.30 * k, segments=16, cap_end=False, smooth_normals=True)
    mesh.cylinder_z(cx, cy, cz + 0.52 * k, cz + 0.72 * k, 0.23 * k, 0.25 * k, segments=16, cap_start=False, cap_end=False, smooth_normals=True)
    mesh.chamfered_box((cx, cy, cz + 0.36 * k), (0.64 * k, 0.56 * k, 0.55 * k), bevel=0.07 * k, tile=2.0)


PART_LIBRARY = {
    "hull_core": part_hull_core,
    "wing": part_wing,
    "engine_pod": part_engine_pod,
}


def mirror_x(mesh: Mesh, source: Mesh) -> None:
    """Append a mirrored copy of source across the YZ plane into mesh."""
    base = len(mesh.positions) // 3
    for i in range(len(source.positions) // 3):
        x, y, z = source.positions[i * 3:i * 3 + 3]
        nx, ny, nz = source.normals[i * 3:i * 3 + 3]
        mesh.vertex((-x, y, z), v_norm((-nx, ny, nz)), (source.uvs[i * 2], source.uvs[i * 2 + 1]))
    for t in range(0, len(source.indices), 3):
        a, b, c = source.indices[t:t + 3]
        mesh.indices.extend([base + a, base + c, base + b])  # mirror flips winding


def assemble_parts(part_name: str, sockets: list[Socket]) -> Mesh:
    mesh = Mesh()
    builder = PART_LIBRARY[part_name]
    for s in sockets:
        local = Mesh()
        builder(local, Socket(s.name, s.position, s.scale, False))
        if s.mirror_x:
            mirror_x(mesh, local)
            # also place the original
            base = len(mesh.positions) // 3
            for i in range(len(local.positions) // 3):
                p = local.positions[i * 3:i * 3 + 3]
                n = local.normals[i * 3:i * 3 + 3]
                mesh.vertex(tuple(p), tuple(n), (local.uvs[i * 2], local.uvs[i * 2 + 1]))
            for idx in local.indices:
                mesh.indices.append(base + idx)
        else:
            base = len(mesh.positions) // 3
            for i in range(len(local.positions) // 3):
                p = local.positions[i * 3:i * 3 + 3]
                n = local.normals[i * 3:i * 3 + 3]
                mesh.vertex(tuple(p), tuple(n), (local.uvs[i * 2], local.uvs[i * 2 + 1]))
            for idx in local.indices:
                mesh.indices.append(base + idx)
    return mesh


SHIP_KITBASH_LAYOUT = {
    "hull_core": [Socket("hull", (0.0, 0.0, -0.1))],
    "wing": [Socket("wing_r", (1.35, -0.04, 0.55)), Socket("wing_l", (-1.35, -0.04, 0.55), mirror_x=False)],
    "engine_pod": [Socket("eng_l", (-0.45, 0.0, 1.35)), Socket("eng_r", (0.45, 0.0, 1.35))],
}


def build_ship_kitbash(tex_size: int = 512) -> bytes:
    """Kitbash variant assembled from data-driven part sockets."""
    glb = GlbBuilder()
    hull_img = glb.image(make_inked_hull_png(tex_size))
    wing_img = glb.image(make_inked_wing_png(tex_size))
    canopy_img = glb.image(make_canopy_png(max(128, tex_size // 2)))
    core_img = glb.image(make_thruster_core_png(max(128, tex_size // 2)))
    core_emissive_img = glb.image(make_thruster_emissive_png(max(128, tex_size // 2)))
    nav_emissive_img = glb.image(make_nav_emissive_png(max(64, tex_size // 4)))

    hull_mat = glb.material("hull-armor", [0.58, 0.63, 0.72, 1.0], 0.55, 0.35, image=hull_img)
    wing_mat = glb.material("wing-trim", [0.55, 0.60, 0.70, 1.0], 0.45, 0.42, image=wing_img)
    canopy_mat = glb.material("canopy-glass", [0.22, 0.55, 0.85, 1.0], 0.15, 0.18, image=canopy_img)
    housing_mat = glb.material("engine-housing", [0.26, 0.29, 0.36, 1.0], 0.75, 0.38, image=hull_img)
    core_mat = glb.material("engine-core", [0.03, 0.035, 0.045, 1.0], 0.0, 0.9, image=core_img,
                            emissive_image=core_emissive_img, emissive_factor=[0.35, 0.9, 1.0, 1.0])
    nav_mat = glb.material("nav-lights", [0.95, 0.22, 0.16, 1.0], 0.0, 0.6,
                           emissive_image=nav_emissive_img, emissive_factor=[1.2, 0.25, 0.15, 1.0])

    glb.mesh("kitbash-hull", assemble_parts("hull_core", SHIP_KITBASH_LAYOUT["hull_core"]), hull_mat)
    # wings: mirror right wing to left
    right = Mesh(); part_wing(right, Socket("wing_r", (1.35, -0.04, 0.55)))
    wings = Mesh()
    wings.positions.extend(right.positions); wings.normals.extend(right.normals); wings.uvs.extend(right.uvs); wings.indices.extend(right.indices)
    mirror_x(wings, right)
    glb.mesh("kitbash-wings", wings, wing_mat)

    canopy = Mesh(); canopy.box((0.0, 0.35, -0.6), (0.55, 0.28, 0.9), tile=1.0)
    glb.mesh("kitbash-canopy", canopy, canopy_mat)

    glb.mesh("kitbash-engine-housings", assemble_parts("engine_pod", SHIP_KITBASH_LAYOUT["engine_pod"]), housing_mat)

    cores = Mesh()
    for cx in (-0.45, 0.45):
        cores.cylinder_z(cx, 0.0, 1.95, 2.05, 0.18, 0.18, segments=16, cap_start=False, smooth_normals=True)
    glb.mesh("kitbash-engine-cores", cores, core_mat)

    nav = Mesh()
    for x in (-2.45, 2.45):
        nav.box((x, -0.03, 0.95), (0.05, 0.05, 0.12))
    glb.mesh("kitbash-nav-lights", nav, nav_mat)

    return glb.build()
