"""Mesh primitives and vector helpers for procedural asset generation."""
from __future__ import annotations

import math


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
    length = math.sqrt(v_dot(v, v))
    if length <= 1e-8:
        return (0.0, 0.0, 1.0)
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

    def quad_smooth(self, p0, p1, p2, p3, n0, n1, n2, n3,
                    uv0=(0.0, 0.0), uv1=(1.0, 0.0),
                    uv2=(1.0, 1.0), uv3=(0.0, 1.0), outward_hint=None):
        """Quad with per-vertex normals (for smooth round surfaces)."""
        n = v_norm(v_cross(v_sub(p1, p0), v_sub(p2, p0)))
        if outward_hint is not None and v_dot(n, outward_hint) < 0.0:
            p1, p3 = p3, p1
            n1, n3 = n3, n1
            uv1, uv3 = uv3, uv1
            n = (-n[0], -n[1], -n[2])
        if v_dot(n0, n) < 0.0: n0 = (-n0[0], -n0[1], -n0[2])
        if v_dot(n1, n) < 0.0: n1 = (-n1[0], -n1[1], -n1[2])
        if v_dot(n2, n) < 0.0: n2 = (-n2[0], -n2[1], -n2[2])
        if v_dot(n3, n) < 0.0: n3 = (-n3[0], -n3[1], -n3[2])
        i0 = self.vertex(p0, n0, uv0)
        i1 = self.vertex(p1, n1, uv1)
        i2 = self.vertex(p2, n2, uv2)
        i3 = self.vertex(p3, n3, uv3)
        self.triangle(i0, i1, i2)
        self.triangle(i0, i2, i3)

    def sphere(self, center, radius, segments=12, rings=8, squash=1.0):
        """Smooth UV sphere. Outward CCW winding, used for round nav lamps."""
        base = len(self.positions) // 3
        for j in range(rings + 1):
            theta = math.pi * j / rings
            st, ct = math.sin(theta), math.cos(theta)
            for i in range(segments + 1):
                phi = 2.0 * math.pi * i / segments
                n = (st * math.cos(phi), ct * squash, st * math.sin(phi))
                n = v_norm(n)
                p = (center[0] + radius * st * math.cos(phi),
                     center[1] + radius * ct * squash,
                     center[2] + radius * st * math.sin(phi))
                self.vertex(p, n, (i / segments, j / rings))
        for j in range(rings):
            for i in range(segments):
                a = base + j * (segments + 1) + i
                b = a + 1
                c = a + segments + 1
                d = c + 1
                if j != 0:
                    self.triangle(a, b, c)
                if j != rings - 1:
                    self.triangle(b, d, c)

    def box(self, center, size, tile=2.0):
        """Box with world-scale UVs: `tile` is world units per texture repeat.

        A large engine housing now repeats the inked panel map several times
        instead of stretching one 64px cell across the whole face (the old
        "Lego brick" look).
        """
        cx, cy, cz = center
        sx, sy, sz = (s * 0.5 for s in size)
        tile = max(tile, 0.05)
        faces = [
            ((0, 0, 1),  [(-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz)], size[0], size[1]),
            ((0, 0, -1), [(sx, -sy, -sz), (-sx, -sy, -sz), (-sx, sy, -sz), (sx, sy, -sz)], size[0], size[1]),
            ((1, 0, 0),  [(sx, -sy, sz), (sx, -sy, -sz), (sx, sy, -sz), (sx, sy, sz)], size[2], size[1]),
            ((-1, 0, 0), [(-sx, -sy, -sz), (-sx, -sy, sz), (-sx, sy, sz), (-sx, sy, -sz)], size[2], size[1]),
            ((0, 1, 0),  [(-sx, sy, sz), (sx, sy, sz), (sx, sy, -sz), (-sx, sy, -sz)], size[0], size[2]),
            ((0, -1, 0), [(-sx, -sy, -sz), (sx, -sy, -sz), (sx, -sy, sz), (-sx, -sy, sz)], size[0], size[2]),
        ]
        for normal, corners, faceW, faceH in faces:
            pts = [(cx + p[0], cy + p[1], cz + p[2]) for p in corners]
            su, sv = faceW / tile, faceH / tile
            self.quad(
                pts[0], pts[1], pts[2], pts[3],
                (0.0, 0.0), (su, 0.0), (su, sv), (0.0, sv),
                outward_hint=normal,
            )

    def chamfered_box(self, center, size, bevel=0.08, tile=2.0):
        """Box with chamfered edges: 6 inset faces + 12 edge strips + corners.

        Cel highlights need bevels to catch the key light; razor-sharp boxes
        never show a specular edge.
        """
        cx, cy, cz = center
        sx, sy, sz = (s * 0.5 for s in size)
        b = min(bevel, sx * 0.4, sy * 0.4, sz * 0.4)
        # Bevel strips/corners are too thin to carry panel patterns. Pin to a flat,
        # ink-free interior pixel of the 256x256 hull texture (16,16).
        flat_uv = (0.0625, 0.0625)
        # Inset face quads (y/z etc.), offset inward by b on the other axes.
        faces = [
            ((0, 0, 1),  [(-sx+b, -sy+b, sz), (sx-b, -sy+b, sz), (sx-b, sy-b, sz), (-sx+b, sy-b, sz)], size[0]-2*b, size[1]-2*b),
            ((0, 0, -1), [(sx-b, -sy+b, -sz), (-sx+b, -sy+b, -sz), (-sx+b, sy-b, -sz), (sx-b, sy-b, -sz)], size[0]-2*b, size[1]-2*b),
            ((1, 0, 0),  [(sx, -sy+b, sz-b), (sx, -sy+b, -sz+b), (sx, sy-b, -sz+b), (sx, sy-b, sz-b)], size[2]-2*b, size[1]-2*b),
            ((-1, 0, 0), [(-sx, -sy+b, -sz+b), (-sx, -sy+b, sz-b), (-sx, sy-b, sz-b), (-sx, sy-b, -sz+b)], size[2]-2*b, size[1]-2*b),
            ((0, 1, 0),  [(-sx+b, sy, sz-b), (sx-b, sy, sz-b), (sx-b, sy, -sz+b), (-sx+b, sy, -sz+b)], size[0]-2*b, size[2]-2*b),
            ((0, -1, 0), [(-sx+b, -sy, -sz+b), (sx-b, -sy, -sz+b), (sx-b, -sy, sz-b), (-sx+b, -sy, sz-b)], size[0]-2*b, size[2]-2*b),
        ]
        for normal, corners, faceW, faceH in faces:
            pts = [(cx + p[0], cy + p[1], cz + p[2]) for p in corners]
            su, sv = faceW / tile, faceH / tile
            self.quad(pts[0], pts[1], pts[2], pts[3], (0,0), (su,0), (su,sv), (0,sv), outward_hint=normal)

        # 12 edge strips bridging adjacent inset faces. Hints must have a component
        # along both adjacent face axes, otherwise quad() gets dot(normal,hint)==0.
        edges = []
        for sy_sign in (-1, 1):
            for sz_sign in (-1, 1):
                edges.append(([(sx-b, sy_sign*sy, sz_sign*(sz-b)), (-sx+b, sy_sign*sy, sz_sign*(sz-b)),
                               (-sx+b, sy_sign*(sy-b), sz_sign*sz), (sx-b, sy_sign*(sy-b), sz_sign*sz)],
                              (0, sy_sign, sz_sign)))
        for sx_sign in (-1, 1):
            for sz_sign in (-1, 1):
                edges.append(([(sx_sign*sx, sy-b, sz_sign*(sz-b)), (sx_sign*sx, -sy+b, sz_sign*(sz-b)),
                               (sx_sign*(sx-b), -sy+b, sz_sign*sz), (sx_sign*(sx-b), sy-b, sz_sign*sz)],
                              (sx_sign, 0, sz_sign)))
        for sx_sign in (-1, 1):
            for sy_sign in (-1, 1):
                edges.append(([(sx_sign*(sx-b), sy_sign*sy, -sz+b), (sx_sign*(sx-b), sy_sign*sy, sz-b),
                               (sx_sign*sx, sy_sign*(sy-b), sz-b), (sx_sign*sx, sy_sign*(sy-b), -sz+b)],
                              (sx_sign, sy_sign, 0)))
        for corners, hint in edges:
            pts = [(cx + p[0], cy + p[1], cz + p[2]) for p in corners]
            self.quad(pts[0], pts[1], pts[2], pts[3], flat_uv, flat_uv, flat_uv, flat_uv, outward_hint=hint)

        # 8 corner triangles. Winding depends on octant handedness.
        for sx_sign in (-1, 1):
            for sy_sign in (-1, 1):
                for sz_sign in (-1, 1):
                    hint = (sx_sign, sy_sign, sz_sign)
                    a = (cx + sx_sign*sx, cy + sy_sign*(sy-b), cz + sz_sign*(sz-b))
                    bpt = (cx + sx_sign*(sx-b), cy + sy_sign*sy, cz + sz_sign*(sz-b))
                    cpt = (cx + sx_sign*(sx-b), cy + sy_sign*(sy-b), cz + sz_sign*sz)
                    n = v_cross(v_sub(bpt, a), v_sub(cpt, a))
                    tri = (a, bpt, cpt) if v_dot(n, hint) >= 0.0 else (a, cpt, bpt)
                    n = v_norm(n if v_dot(n, hint) >= 0.0 else (-n[0], -n[1], -n[2]))
                    self.triangle(*[self.vertex(p, n, flat_uv) for p in tri])

    def box_rotated(self, center, size, angle):
        """Axis-aligned box rotated about Z by `angle`, keeping outward winding."""
        temp = Mesh()
        temp.box((0.0, 0.0, 0.0), size)
        base = len(self.positions) // 3
        c, sn = math.cos(angle), math.sin(angle)
        for i in range(len(temp.positions) // 3):
            x, y, z = temp.positions[i * 3:i * 3 + 3]
            nx, ny, nz = temp.normals[i * 3:i * 3 + 3]
            self.vertex(
                (center[0] + x * c - y * sn, center[1] + x * sn + y * c, center[2] + z),
                (nx * c - ny * sn, nx * sn + ny * c, nz),
                (temp.uvs[i * 2], temp.uvs[i * 2 + 1]),
            )
        for idx in temp.indices:
            self.indices.append(base + idx)

    def loft(self, rings, uv_u_scale=1.0, uv_v_scale=1.0, cap_start=True, cap_end=True,
             smooth_normals=False):
        """Lofts a sequence of closed 3D polygon rings with outward normals.

        V is distributed by cumulative ring-center arc length so texture density
        stays uniform even when ring spacing varies. Set smooth_normals=True for
        round primitives (cylinders/toruses) to avoid faceted cel bands.
        """
        ring_count = len(rings)
        seg_count = len(rings[0])
        # Cumulative arc length for uniform V distribution.
        centers = []
        for ring in rings:
            centers.append((
                sum(p[0] for p in ring) / seg_count,
                sum(p[1] for p in ring) / seg_count,
                sum(p[2] for p in ring) / seg_count,
            ))
        arc = [0.0]
        for i in range(1, ring_count):
            d = v_sub(centers[i], centers[i - 1])
            arc.append(arc[-1] + math.sqrt(v_dot(d, d)))
        total_arc = arc[-1] if arc[-1] > 1e-6 else 1.0

        for r_idx in range(ring_count - 1):
            r0 = rings[r_idx]
            r1 = rings[r_idx + 1]
            v0 = (arc[r_idx] / total_arc) * uv_v_scale
            v1 = (arc[r_idx + 1] / total_arc) * uv_v_scale
            c0 = centers[r_idx]
            c1 = centers[r_idx + 1]
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
                if smooth_normals:
                    n00 = v_norm(v_sub(r0[s], c0))
                    n01 = v_norm(v_sub(r0[sn], c0))
                    n11 = v_norm(v_sub(r1[sn], c1))
                    n10 = v_norm(v_sub(r1[s], c1))
                    self.quad_smooth(
                        r0[s], r0[sn], r1[sn], r1[s],
                        n00, n01, n11, n10,
                        (u0, v0), (u1, v0), (u1, v1), (u0, v1),
                        outward_hint=outward,
                    )
                else:
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
                # Sample the inked corner (0,0) so end caps read as dark
                # armoured plates instead of bright white triangle fans.
                ic = self.vertex(center, out_dir, (0.0, 0.0))
                ia = self.vertex(a, out_dir, (0.0, 0.0))
                ib = self.vertex(b, out_dir, (0.0, 0.0))
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
                # Sample the inked corner (0,0) so end caps read as dark
                # armoured plates instead of bright white triangle fans.
                ic = self.vertex(center, out_dir, (0.0, 0.0))
                ia = self.vertex(a, out_dir, (0.0, 0.0))
                ib = self.vertex(b, out_dir, (0.0, 0.0))
                self.triangle(ic, ia, ib)

    def cylinder_z(self, cx, cy, z0, z1, r0, r1, segments=16, cap_start=True, cap_end=True,
                   uv_u=None, uv_v=None, tile=2.0, smooth_normals=False):
        """Cylinder along Z. UVs default to world-scale repeats based on `tile`."""
        rings = []
        for z, r in ((z0, r0), (z1, r1)):
            ring = []
            for i in range(segments):
                ang = (i / segments) * 2.0 * math.pi
                ring.append((cx + r * math.cos(ang), cy + r * math.sin(ang), z))
            rings.append(ring)
        if uv_u is None:
            uv_u = (2.0 * math.pi * ((r0 + r1) * 0.5)) / max(tile, 0.05)
        if uv_v is None:
            uv_v = abs(z1 - z0) / max(tile, 0.05)
        self.loft(rings, uv_u_scale=uv_u, uv_v_scale=uv_v, cap_start=cap_start, cap_end=cap_end,
                  smooth_normals=smooth_normals)

    def torus_ring_xy(self, major_r, tube_rx, tube_rz, major_segs=48, tube_sides=8, uv_u=None, tile=2.0,
                       smooth_normals=False):
        """Builds a solid 3D hexagonal/octagonal habitat ring in the XY plane."""
        tube_angles = [((j + 0.5) / tube_sides) * 2.0 * math.pi for j in range(tube_sides)]
        tube_circ = 2.0 * math.pi * math.sqrt((tube_rx * tube_rx + tube_rz * tube_rz) * 0.5)
        major_circ = 2.0 * math.pi * major_r
        uv_u_scale = (major_circ / max(tile, 0.05)) if uv_u is None else uv_u
        uv_v_scale = tube_circ / max(tile, 0.05)
        for i in range(major_segs):
            a0 = (i / major_segs) * 2.0 * math.pi
            a1 = ((i + 1) / major_segs) * 2.0 * math.pi
            u0 = (i / major_segs) * uv_u_scale
            u1 = ((i + 1) / major_segs) * uv_u_scale
            c0 = (major_r * math.cos(a0), major_r * math.sin(a0), 0.0)
            c1 = (major_r * math.cos(a1), major_r * math.sin(a1), 0.0)
            for j in range(tube_sides):
                jn = (j + 1) % tube_sides
                t0 = tube_angles[j]
                t1 = tube_angles[jn]
                v0 = (j / tube_sides) * uv_v_scale
                v1 = ((j + 1) / tube_sides) * uv_v_scale

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
                outward = v_sub(mid, c_mid)
                if smooth_normals:
                    n00 = v_norm(v_sub(p00, c0))
                    n10 = v_norm(v_sub(p10, c1))
                    n11 = v_norm(v_sub(p11, c1))
                    n01 = v_norm(v_sub(p01, c0))
                    self.quad_smooth(
                        p00, p10, p11, p01,
                        n00, n10, n11, n01,
                        (u0, v0), (u1, v0), (u1, v1), (u0, v1),
                        outward_hint=outward,
                    )
                else:
                    self.quad(
                        p00, p10, p11, p01,
                        (u0, v0), (u1, v0), (u1, v1), (u0, v1),
                        outward_hint=outward,
                    )


