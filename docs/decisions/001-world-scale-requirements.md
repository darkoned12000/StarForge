# 001 – World Scale Requirements (step 2.0)

Status: **agreed**. Acceptance criteria for M2 steps 2.1–2.5.

## Intent

Exploration with no zones or sectors: pick a direction, fly for hours,
and find something (an asteroid field not on any map). The engine does
not need to support billions of destinations, but a large solar system
or star system must be traversable without loading boundaries, and the
design must not preclude going further.

## Requirements

| Item | Requirement | Status |
|------|-------------|--------|
| Required extent | Large star system including outer regions: at least 1e15 m (~0.1 ly) from any origin | Confirm |
| Design reach | Do not preclude interstellar travel; int64 cells reach ~1e25 m | Proposed |
| Near-camera resolution | Sub-millimetre local precision at any distance from the origin | Confirm |
| Determinism | Bit-exact across machines and visit orders; integers only in the generation path, no floating-point values feeding hashes | Proposed |
| Loading | No visible loading; content near the camera generated on demand | Required |
| Persistence | Store only player deltas, keyed by cell and object ID | Proposed |
| GPU path | Positions reach shaders as camera-relative float32; shader `double` and `int64` are not relied on | Proposed |
| Large objects | Camera near the surface of a body with radius ~6.4e6 m must not jitter | Required |
| Units | 1.0 = 1 metre | Proposed |

## Reference numbers

Spacing between adjacent representable values (so positional error is
at least half of this):

| Distance | float | double |
|----------|-------|--------|
| 1e6 m | 6 cm | negligible |
| 1e9 m | 64 m | negligible |
| 1e12 m | 65 km | 0.12 mm |
| 4.5e12 m (Neptune orbit) | worse | ~0.5 mm |
| 1e13 m | n/a | ~2 mm |
| 1e15 m | n/a | 12.5 cm |
| 1e16 m (~1 ly) | n/a | 2 m |
| 1e21 m | n/a | 131 km |

Plain double reaches millimetre precision only to about 1e13 m. That
covers the inner solar system but not the outer regions or interstellar
space.

## Candidates

| Candidate | Reach at sub-mm | Deterministic addressing | Notes |
|-----------|-----------------|--------------------------|-------|
| float | ~1e4 m | no | Insufficient |
| double | ~1e12 m | no (rounding feeds hashes) | Simple; fails the required extent |
| int64 millimetres | ~1e16 m | yes | Exact; ends near 1 ly; no scale hierarchy |
| int64 cell (power of two, e.g. 2^20 m) + double offset | ~1e25 m | yes | Coarse cell index at any scale is a shift; one position resolves at every scale |

Leading candidate: **cell + offset**, because procedural exploration
needs exact integer addresses and the same design supplies the scale
hierarchy. Confirm with the step 2.1 table before implementing.

## Open questions

1. Is the 1e15 m required extent right, or is the inner/outer solar
   system enough for the first game?
2. Will there be faster-than-light or warp travel? Flight distance in
   10 hours: 1,000 km/s → 3.6e10 m; 0.1c → 1.1e12 m; c → 1.1e13 m;
   100c → 1.1e15 m. Sub-light-only flight stays within double territory
   per session; the design reach still matters for persistence and
   determinism.
3. Cell size: 2^20 m suits asteroid fields; confirm the scale levels
   the generator needs (fields ~1e7 m, stars ~1e16 m apart).
