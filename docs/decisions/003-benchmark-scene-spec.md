# 003 – Benchmark Scene Specification (M4.3a)

Status: **agreed**. Acceptance criteria for M4.3b–d.

## Intent

Define a reproducible, deterministic benchmark scene that exercises CPU-driven
rendering at various instance counts and visibility fractions. The scene must be
simple enough to implement quickly but complex enough to stress the CPU
submission path.

## Scene profiles

### Profile A — Submission stress
- **Mesh:** Single cube (36 vertices, 12 triangles, per-face colors).
- **Distribution:** Uniform random in a sphere of radius R around origin.
- **Camera:** Fixed at (0, 0, 5), looking down -Z (towards origin), FOV 60°, aspect 16:9.
- **Resolution:** 1920×1080.
- **Instance counts:** 10k, 100k, 1M.
- **Visible fractions:** ~100%, ~10%, ~1% (achieved by sphere radius R relative
  to camera distance; R = 5.0 / cbrt(visibleFraction)).
- **Seed:** 42 (deterministic generation).
- **Depth:** Reverse-Z, far plane at 10000 m, D32_SFLOAT.

### Profile B — Geometry/raster stress
- **Mesh:** Single cube (same as Profile A).
- **Distribution:** Dense grid filling the view frustum.
- **Camera:** Fixed at (0, 0, 5), looking down -Z, FOV 60°, aspect 16:9.
- **Resolution:** 1920×1080.
- **Instance counts:** 10k, 100k.
- **Visible fractions:** ~100%, ~10%.
- **Seed:** 42.

## Visibility control

Visibility fractions are controlled by placing instances in a sphere of radius R
around the origin and positioning the camera at (0, 0, 5) looking down -Z.
The sphere radius is computed as R = 5.0 / cbrt(visibleFraction), which
approximates the desired visible fraction:

- ~100% visible: R ≈ 5.0 m (sphere fits within view frustum)
- ~10% visible: R ≈ 10.77 m
- ~1% visible: R ≈ 23.21 m

Both baselines (naive and competent) render the **same visible set**. The naive
baseline submits all instances (GPU rejects off-screen via frustum/depth);
the competent baseline culls on CPU before submission.

## Baselines

### Naive baseline
- No CPU culling.
- One draw call per visible instance (vkCmdDraw, 36 vertices, no instancing).
- Push constants: per-draw MVP matrix (camera-relative, composed in double).
- No depth pre-pass.
- Submits all instances in the scene (GPU handles visibility via frustum/depth).

### Competent CPU-driven baseline
- CPU frustum culling (AABB or sphere per instance).
- Visible instances grouped into instanced draws (vkCmdDraw with instanceCount).
- Per-instance data in a storage buffer (BDA) for transform.
- Push constants: shared MVP for the instanced draw.
- Multi-draw indirect optional (recorded separately if available).

### Optional stress test
- Naive 1M-instance stress test (clearly labelled "stress", only if runtime
  and memory allow). Not a required baseline.

## Scene generation

Instances are generated deterministically from seed 42:
- Position: uniform random in sphere of radius R
- Rotation: random quaternion (uniform on SO(3))
- Scale: uniform in [0.5, 2.0]

Camera path: fixed at origin, looking down +Z. No animation.

## Metrics recorded
- CPU command recording time (ms)
- CPU submission time (ms)
- GPU execution time (ms, via timestamps)
- Frame time (ms, wall-clock)
- Draw calls submitted
- Instances submitted
- Instances visible (competent baseline only)
- VMA memory allocated/used

## Acceptance
- Scene spec agreed and committed (this document).
- Both baselines produce valid, non-zero renders.
- Baseline curves recorded for 10k, 100k, 1M × visibility fractions.
- Per-machine baseline files committed with full metadata.