# ADR-0011: Procedural generation pipeline, determinism, and caching

- **Status:** Accepted (M13.1); noise-scope wording superseded by ADR-0012.
- **Context:** M13 turns the M6 pure generator into a runtime streaming
  system that survives a moving camera at 60 fps. The project’s
  toon/cel-shaded art direction and M2 precision contract add two
  hard constraints: procedural features must not rely on floating-point
  SIMD noise (cross-platform bit-exactness), and LOD transitions must
  not produce visible pops or chunk-seam lighting discontinuities.
- **Decision:**
  - **Deterministic integer noise, no FastNoise2:** all procedural
    randomness flows from integer seed/lattice hashing and a
    `DeterministicRng` (xorshift64). The project compiles deterministic
    targets with `-ffp-contract=off` and forbids architecture-specific
    SIMD noise, so a cell's bytes are identical across runs and across
    x86-64/ARM64. Stylized surfaces use macro-facets, geometric plates,
    and step features; micro-detail lives in decals, hatching, or flat
    curvature bands — never in generation geometry.
  - **Chunk-seam normal contract:** where adjacent cells share a
    boundary, normals are computed by a shared analytic derivative (or
    unified boundary stencil), never independently per chunk. In cel
    pipelines, independent per-chunk normals create visible seams along
    quantization boundaries.
  - **Toon LOD transitions:** discrete LOD switches happen only inside
    a hysteresis band using dithered crossfade or silhouette-preserving
    geomorphs. The outline pass ignores fading-in duplicate geometry.
  - **Frame budgets are built into the plan:** generation time,
    per-cell object caps, and Vulkan upload (staging bytes + draw calls)
    are capped *before* recording. Queuing never defers into a measured
    frame.
  - **Three-tier ownership:**
    1. **Raw generation store** — immutable keyed blobs
       (`seed → ContentChunk`), owned by `src/content/`.
    2. **GPU buffer cache** — uploaded vertex/index allocations, owned
       by the renderer.
    3. **Active ECS instances** — ephemeral simulation entities, owned
       by the game layer.
    Eviction policy (LRU/distance-first, budgets) lives in the
    scene/streaming director, not the generator.
- **Deltas are a read-time overlay:** `final = ApplyDelta(base, DeltaMap[cell])`
    evaluated when instantiating ECS entities. Base chunks stay
    immutable for byte-checksum tests and reset/respawn debugging;
    saved deltas are stored sparse, never mutated into cached bytes.
  - **Generation describes state, rendering decides appearance:**
    `ContentObject` carries type, subtype, position, scale, and orientation,
    not mesh topology or material styling. Authored/generated geometry and
    toon materials live in their own layers so a later visual direction can
    change without rewriting the world generator.
- **Alternatives considered:**
  - **FastNoise2 (rejected):** fastest quality noise, but SIMD
    floating-point paths (AVX2/AVX-512/NEON) make bit-identical outputs
    fragile on strict cross-platform determinism.
  - **Mutable cached bytes with delta-applied writes (rejected):**
    simpler to write, but breaks checksum verification, complicates
    concurrent reads, and prevents clean reset/respawn.
- **Consequences:**
  - M13 is an operational milestone: first proof is a 2-minute,
    1,000 m/s flythrough at 60 fps with zero validation errors; deltas
    and full exit proof follow it.
  - The generator stays a pure, thread-safe, dependency-free lookup —
    determinism tests are cheap and machine-independent.
