"""Deterministic procedural texture generators."""
from __future__ import annotations

import math
import struct
import zlib


def clamp_byte(v: float) -> int:
    return max(0, min(255, int(v)))


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def make_png_rows(width: int, height: int, row_fn) -> bytes:
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(row_fn(y))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 9)
    out = bytearray(b"\x89PNG\r\n\x1a\n")
    out.extend(png_chunk(b"IHDR", ihdr))
    out.extend(png_chunk(b"IDAT", idat))
    out.extend(png_chunk(b"IEND", b""))
    return bytes(out)


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
                glint = 42 if diag < 0.045 else (18 if diag < 0.11 else 0)
                r = clamp_byte(24 + glint * 0.6)
                g = clamp_byte(75 + glint * 0.9)
                b = clamp_byte(95 + glint)
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
            r = clamp_byte(25 + 175 * (core ** 2.2))
            g = clamp_byte(110 + 110 * core)
            b = 255
            idx = x * 4
            row[idx] = r
            row[idx + 1] = g
            row[idx + 2] = b
            row[idx + 3] = 255
        return bytes(row)

    return make_png_rows(size, size, row_fn)


def make_thruster_emissive_png(size: int = 128) -> bytes:
    """Emissive mask for engine core: bright cyan-white center, black edges."""
    half = (size - 1) * 0.5
    def row_fn(y: int) -> bytes:
        row = bytearray(size * 4)
        dy = (y - half) / half
        for x in range(size):
            dx = (x - half) / half
            dist = min(1.0, math.sqrt(dx * dx + dy * dy))
            core = max(0.0, 1.0 - dist) ** 1.8
            r = clamp_byte(90 * core)
            g = clamp_byte(220 * core)
            b = clamp_byte(255 * core)
            idx = x * 4
            row[idx] = r; row[idx + 1] = g; row[idx + 2] = b; row[idx + 3] = 255
        return bytes(row)
    return make_png_rows(size, size, row_fn)


def make_nav_emissive_png(size: int = 64) -> bytes:
    """Small red emissive mask for navigation lamps."""
    half = (size - 1) * 0.5
    def row_fn(y: int) -> bytes:
        row = bytearray(size * 4)
        dy = (y - half) / half
        for x in range(size):
            dx = (x - half) / half
            dist = min(1.0, math.sqrt(dx * dx + dy * dy))
            glow = max(0.0, 1.0 - dist) ** 1.5
            idx = x * 4
            row[idx] = clamp_byte(255 * glow)
            row[idx + 1] = clamp_byte(60 * glow)
            row[idx + 2] = clamp_byte(30 * glow)
            row[idx + 3] = 255
        return bytes(row)
    return make_png_rows(size, size, row_fn)


def make_station_window_emissive_png(size: int = 256) -> bytes:
    """Emissive mask matching station window bands (warm/cool lit panes)."""
    def row_fn(y: int) -> bytes:
        row = bytearray(size * 4)
        gy = y % 64
        for x in range(size):
            gx = x % 32
            lit = (18 <= gy <= 24 or 40 <= gy <= 46) and (4 <= gx <= 26) and (gx % 6 != 0)
            if lit:
                if ((x // 32) + (y // 64)) % 4 == 0:
                    r, g, b = 120, 255, 90
                else:
                    r, g, b = 255, 220, 60
            else:
                r, g, b = 0, 0, 0
            idx = x * 4
            row[idx] = r; row[idx + 1] = g; row[idx + 2] = b; row[idx + 3] = 255
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
                # Brighter yellow and green interior lighting
                if ((x // 32) + (y // 64)) % 4 == 0:
                    r, g, b = 165, 255, 130
                else:
                    r, g, b = 255, 232, 70
            else:
                r, g, b = 42, 48, 58
            idx = x * 4
            row[idx] = r
            row[idx + 1] = g
            row[idx + 2] = b
            row[idx + 3] = 255
        return bytes(row)

    return make_png_rows(size, size, row_fn)


