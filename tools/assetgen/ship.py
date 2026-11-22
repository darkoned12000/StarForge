"""Hero ship procedural asset build."""
from __future__ import annotations

import math

from .gltf import GlbBuilder
from .mesh import Mesh
from .textures import (
    make_canopy_png,
    make_inked_hull_png,
    make_inked_wing_png,
    make_nav_emissive_png,
    make_thruster_core_png,
    make_thruster_emissive_png,
)


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


def build_ship(tex_size: int = 512) -> bytes:
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
    # Recessed nozzle interior: dark base with emissive mask for bloom-capable glow.
    core_mat = glb.material(
        "engine-core", [0.03, 0.035, 0.045, 1.0], 0.0, 0.9, image=core_img,
        emissive_image=core_emissive_img, emissive_factor=[0.35, 0.9, 1.0, 1.0]
    )
    # Nav markers: small emissive geometry, safe for additive glow pipeline.
    nav_mat = glb.material(
        "nav-lights", [0.95, 0.22, 0.16, 1.0], 0.0, 0.6,
        emissive_image=nav_emissive_img, emissive_factor=[1.2, 0.25, 0.15, 1.0]
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

    hull.chamfered_box((0.0, 0.34, 0.85), (0.24, 0.14, 1.45), bevel=0.03, tile=2.0)
    fin_rings = [
        [(-0.04, 0.38, 0.55), (0.04, 0.38, 0.55), (0.04, 0.44, 1.55), (-0.04, 0.44, 1.55)],
        [(-0.02, 0.86, 1.25), (0.02, 0.86, 1.25), (0.02, 0.88, 1.68), (-0.02, 0.88, 1.68)],
    ]
    hull.loft(fin_rings, uv_u_scale=1.0, uv_v_scale=1.0)

    for side in (-1.0, 1.0):
        hull.chamfered_box((side * 0.66, -0.02, 0.25), (0.28, 0.26, 1.35), bevel=0.04, tile=2.0)
        hull.cylinder_z(side * 0.72, -0.04, -1.15, -0.35, 0.035, 0.045, segments=8)
    glb.mesh("hero-hull", hull, hull_mat)

    wings = Mesh()
    build_swept_wing(wings, -1.0)
    build_swept_wing(wings,  1.0)
    for side in (-1.0, 1.0):
        wings.box((side * 2.56, -0.03, 1.32), (0.14, 0.14, 1.05), tile=1.0)
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
        housings.cylinder_z(cx, 0.02, 1.35, 2.08, 0.34, 0.30, segments=16, cap_end=False, smooth_normals=True)
        housings.cylinder_z(cx, 0.02, 1.88, 2.08, 0.23, 0.25, segments=16, cap_start=False, cap_end=False, smooth_normals=True)
        housings.chamfered_box((cx, 0.02, 1.72), (0.64, 0.56, 0.55), bevel=0.07, tile=2.0)
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


