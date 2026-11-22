"""Orbital station procedural asset build."""
from __future__ import annotations

import math

from .gltf import GlbBuilder
from .mesh import Mesh
from .textures import (
    make_inked_hull_png,
    make_station_window_emissive_png,
    make_station_windows_png,
)


def build_station(tex_size: int = 512) -> bytes:
    glb = GlbBuilder()

    hull_img = glb.image(make_inked_hull_png(tex_size))
    win_img = glb.image(make_station_windows_png(tex_size))
    win_emissive_img = glb.image(make_station_window_emissive_png(tex_size))

    hull_mat = glb.material("station-hull", [0.55, 0.60, 0.70, 1.0], 0.60, 0.42, image=hull_img)
    gold_mat = glb.material("station-trim", [0.72, 0.58, 0.28, 1.0], 0.78, 0.28, image=hull_img)
    # Emissive-mapped windows: sampled in the normal toon pass (depth writes
    # preserved) so they can glow/bloom without additive-pipeline artifacts.
    win_mat = glb.material("station-windows", [1.0, 1.0, 0.92, 1.0], 0.2, 0.5,
                           image=win_img, emissive_image=win_emissive_img,
                           emissive_factor=[1.0, 1.0, 1.0, 1.0])
    # Opaque, texture-free red lamps. Additive emissive on this geometry
    # causes depth/culling artifacts; keep emissiveFactor at 0.
    beacon_mat = glb.material("station-beacons", [1.0, 0.14, 0.08, 1.0], 0.0, 0.9)

    hub = Mesh()
    hub.cylinder_z(0.0, 0.0, -1.10, 1.10, 0.95, 0.95, segments=12, uv_u=3.0, uv_v=2.0, smooth_normals=True)
    hub.cylinder_z(0.0, 0.0,  1.10, 2.05, 0.62, 0.48, segments=12, uv_u=2.0, uv_v=1.5, smooth_normals=True)
    hub.cylinder_z(0.0, 0.0,  2.05, 2.65, 0.36, 0.28, segments=10, uv_u=2.0, uv_v=1.0, smooth_normals=True)
    hub.cylinder_z(0.0, 0.0,  2.65, 3.85, 0.06, 0.02, segments=8,  uv_u=1.0, uv_v=1.0, smooth_normals=True)
    hub.cylinder_z(0.0, 0.0, -1.85, -1.10, 0.68, 0.78, segments=12, uv_u=2.0, uv_v=1.0, smooth_normals=True)
    hub.cylinder_z(0.0, 0.0, -3.10, -1.85, 0.05, 0.14, segments=8,  uv_u=1.0, uv_v=1.0, smooth_normals=True)

    # Six radial trusses physically bridge the hub, both habitat rings and
    # the outer docking pods. Everything overlaps at a joint so no piece
    # floats free (the earlier axis-aligned bars looked disconnected).
    for i in range(6):
        ang = i * (math.pi / 3.0)
        ca, sa = math.cos(ang), math.sin(ang)

        def at(radius, perp=0.0, z=0.0):
            return (radius * ca - perp * sa, radius * sa + perp * ca, z)

        # Continuous main spar: hub (r=0.9) -> inner ring (2.55) -> outer ring
        # (3.35) -> pod (3.75). One beam, so it can never read as floating.
        hub.box_rotated(at(2.30), (2.90, 0.20, 0.22), ang)
        # Twin parallel transverse rails above/below the spar.
        for z in (-0.14, 0.14):
            hub.box_rotated(at(2.30, z=z), (2.70, 0.07, 0.07), ang)
        # Cross-brace tying the rails into a rigid truss at mid-span.
        hub.box_rotated(at(1.75), (0.10, 0.46, 0.08), ang)
        hub.box_rotated(at(2.95), (0.10, 0.46, 0.08), ang)
        # Docking pod at the end, overlapping the outer ring and the spar.
        hub.box_rotated(at(3.72), (0.62, 0.54, 0.62), ang)

    glb.mesh("station-hub", hub, hull_mat)

    rings = Mesh()
    rings.torus_ring_xy(major_r=2.55, tube_rx=0.22, tube_rz=0.24, major_segs=48, tube_sides=8, uv_u=16.0, smooth_normals=True)
    rings.torus_ring_xy(major_r=3.35, tube_rx=0.18, tube_rz=0.20, major_segs=56, tube_sides=8, uv_u=20.0, smooth_normals=True)
    glb.mesh("station-rings", rings, win_mat)

    trim = Mesh()
    for i in range(6):
        ang = i * (math.pi / 3.0) + (math.pi / 6.0)
        ca, sa = math.cos(ang), math.sin(ang)
        trim.box((2.55 * ca, 2.55 * sa, 0.30), (0.42, 0.42, 0.20), tile=1.0)
        trim.box((2.55 * ca, 2.55 * sa, -0.30), (0.42, 0.42, 0.20), tile=1.0)
    trim.torus_ring_xy(major_r=0.72, tube_rx=0.07, tube_rz=0.07, major_segs=24, tube_sides=6, uv_u=6.0, smooth_normals=True)
    glb.mesh("station-trim", trim, gold_mat)

    beacons = Mesh()
    # Big navigation lamps at both ends of the hub, plus one on each outer
    # docking pod. Kept OUTSIDE the pod hulls (pod centre r=3.72, half-size
    # 0.31 -> outer face r=4.03) so no lamp is buried inside opaque geometry.
    # Round dome lamps instead of flat squares: smooth spheres read as
    # navigation lights from any angle, with a soft cel highlight on the cap.
    for z in (3.88, -3.14):
        beacons.sphere((0.0, 0.0, z), 0.38, segments=12, rings=8)
        for ang in (0.0, math.pi / 2.0, math.pi, 3.0 * math.pi / 2.0):
            beacons.sphere((0.62 * math.cos(ang), 0.62 * math.sin(ang), z),
                           0.16, segments=10, rings=6)
    # Pod lamps: spheres centred on each docking pod so they wrap the pod's
    # outer face and stay visible from any viewing angle.
    for i in range(6):
        ang = i * (math.pi / 3.0)
        beacons.sphere((3.72 * math.cos(ang), 3.72 * math.sin(ang), 0.0),
                       0.44, segments=12, rings=8)
    glb.mesh("station-beacons", beacons, beacon_mat)

    return glb.build()


