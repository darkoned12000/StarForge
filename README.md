![StarForge - Space game focused library built on Vulkan](screenshots/Starforge_Banner.png)

# STARFORGE

Space-focused rendering engine and game-system foundation, built on Vulkan.

STARFORGE provides the engine plumbing a large-scale 3D space game needs:
precision at astronomical range, deterministic procedural worlds, GPU-driven
object counts, scalable planetary rendering, spatial audio, physics, AI, and
a cel-shaded art direction.

## Project Vision

STARFORGE is designed for 3D space games where naive engine assumptions
about scale, performance, and density fail. Its core goals are:

- **Astronomical scale without precision loss.** A scene at the origin and
  the same scene beyond `10¹² m` should render identically.
- **Large planetary environments.** Orbit-to-surface rendering with
  continuous LOD and stable chunk boundaries.
- **Deterministic procedural worlds.** Content is a pure function of
  position, seed, and scale level, so a player returning to a visited
  location sees a byte-identical world.
- **No loading-zone streaming.** Generated content loads on demand with the
  camera, bounded by frame budgets and upload caps.
- **A readable toon aesthetic for mass scale.** Flat color bands, ink
  outlines, rim light, and graphic HUD elements make huge fleets and
  asteroid fields readable without photorealistic noise.
- **GPU-driven rendering.** Massive object counts should be submitted and
  culled through GPU pipelines, not as thousands of CPU draw calls.

See [docs/VISION.md](docs/VISION.md) for goals and non-goals, and
[docs/ROADMAP.md](docs/ROADMAP.md) for the milestone plan and acceptance
criteria.

## Technology

- **C++26** with Clang
- **CMake 3.30+** and Ninja
- **Vulkan 1.3 minimum, 1.4 preferred**, including dynamic rendering,
  synchronization2, and buffer device address (BDA)
- **Vulkan Memory Allocator (VMA)**
- **Slang shaders**, compiled explicitly as column-major SPIR-V
- **SDL3** for windowing and platform integration
- **GLM**, wrapped behind STARFORGE math aliases
- **Flecs 4.2** for ECS
- **Jolt Physics 5.6** for collisions and dynamics
- **miniaudio 0.11.26** for 3D audio
- **nlohmann JSON** for benchmark data
- **doctest** for CPU/GPU test suites

Cross-platform architecture is a design requirement; Linux / EndeavourOS is
the initial development target.

## Development Platform

Linux / EndeavourOS initially.

The engine uses SDL3 for windowing, which transparently supports both X11
and Wayland. On Wayland compositors (e.g., Hyprland, Sway, GNOME on Wayland),
SDL3 automatically selects the native Wayland backend — no XWayland required.
All Vulkan surface creation, resize handling, and presentation go through
SDL3's abstraction, so the same binary works on X11 and Wayland without
recompilation.

Cross-platform architecture is a design requirement.

## Build

Configure:

    cmake --preset linux-debug

Build:

    cmake --build --preset debug
    cmake --build --preset debug --target check

Run the sandbox:

    ./build/linux-debug/bin/starforge_sandbox

Run the tests:

    ctest --preset debug
    ctest --preset debug --output-on-failure -V

Run only the headless GPU tests (or exclude them on headless CI without a GPU):

    ctest --preset debug -L gpu -V
    ctest --preset debug -LE gpu

Validation layers are enabled in Debug builds only
(`-DSTARFORGE_ENABLE_VALIDATION=OFF` to disable). Unit tests use
doctest: install the `doctest` package, or CMake fetches it automatically.

### Forcing fallback paths

Optional GPU features can be switched off at runtime to exercise
fallback code on capable hardware:

    STARFORGE_DISABLE_FEATURES=meshShader,bindless ./build/linux-debug/bin/starforge_sandbox

Overridable features: `bindless`, `drawIndirectCount`,
`multiDrawIndirect`, `shaderFloat64`, `meshShader`. Hard requirements
cannot be disabled.

## Repository Layout

    include/starforge/   Public, developer-facing headers (see docs/public-api.md)
    src/core/            Logging, version, foundational utilities
    src/math/            STARFORGE vector/matrix aliases and projection helpers
    src/spatial/         WorldPosition, camera-relative transforms, and render objects
    src/ai/              AI components, perception, steering, behavior FSM,
                         traffic routes, and physics island coupling
    src/content/         Deterministic procedural content generation, streaming,
                         bounded cache, LOD, budgets, and player deltas
    src/jobs/            Job scheduler, dependencies, scratch memory, deferred deletion
    src/stats/           Shared statistics helpers
    src/platform/        SDL3 window, surface, and event handling
    src/physics/         Jolt bridge, collision layers, raycasts, sensors, and
                         CCD support
    src/vulkan/          Private Vulkan backend (instance, device, swapchain, renderer)
    src/gpu_resources/   VMA allocator, buffers, images, transfer, compute, culling
    src/planets/         Cubed-sphere and quadtree planet systems, meshes, terrain, atmosphere
    src/environment/     Internal star, nebula, dust, storm, wormhole, FTL, combat,
                         ship, asteroid-field, and other effect renderers
    src/public_api/      Public facades and raw-data bridges
    src/bench/           Benchmark harness, scene/workload generation, results, and compare tool
    apps/sandbox/        Public-API example application
    benchmarks/          Machine-readable frame-time budgets and baseline data
    tests/               CPU unit tests (T1), headless GPU tests (T2/T3), and golden images
    shaders/             Slang shader sources (compute/ and graphics/)
    screenshots/         README and documentation images
    third_party/         Vendored third-party libraries (VMA, GLM, stb, nlohmann, doctest)
    cmake/               Build helper modules
    docs/                Vision, architecture, roadmap, decisions, AI instructions

## Architecture

Vulkan is the initial GPU backend.

The public STARFORGE API must remain independent
of Vulkan implementation details.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/ROADMAP.md](docs/ROADMAP.md).
The developer-facing API, its ownership/error rules, and its pre-1.0
stability policy are documented in [docs/public-api.md](docs/public-api.md);
the sandbox in `apps/sandbox/` is the public API example.
Accepted architectural decisions and unresolved decision points are indexed
in [docs/decisions/](docs/decisions/README.md).

## Implemented features

### World coordinates and GPU rendering

- `WorldPosition` uses int64 cells plus double offsets, reaching beyond
  `10²⁴ m` while preserving sub-millimetre local precision.
- Camera-relative transforms compose positions in double precision and cast
  to float once, preventing large-object vertex jitter.
- Translation-invariance tests cover random positions, negative coordinates,
  cell boundaries, and near-overflow values.
- GPU pipelines use dynamic rendering, depth ordering, synchronization2,
  buffer device address, secondary command buffers, and deferred resource
  retirement.

### Public API

- `Application`, `Renderer`, `Starfield`, `Toon`, and `Audio` facades are
  move-only and backend-neutral.
- `starforge::math` and `starforge::WorldPosition` are GLM/Vulkan-free.
- Shader artifacts are bundled, shader resolution is owned by the API, and
  failures use `std::expected`.
- Public headers are checked by leakage gates and an external-consumer
  compile test.

### Cel-shaded rendering

- Quantized diffuse bands, stepped specular, rim light, and inverted-hull
  outlines.
- Per-draw data is held in a BDA buffer; the toon push-constant block is
  80 bytes, below Vulkan's guaranteed 128-byte minimum.
- A three-stop diffuse ramp supports `shadowColor` → `midColor` → `baseColor`.
- The sandbox demonstrates the public toon material over a starfield.

### Planets and environment

- Double-precision cubed-sphere planet mapping.
- CPU quadtree LOD with screen-space error, hysteresis, sibling 2:1 checks,
  and deterministic parallel traversal.
- Patch geometry with RTC vertices, normals, UVs, and analytical bounds.
- Headless descent and cross-face seam checks.
- Single-scattering atmosphere LUT and shell rendering.
- Starfield, sun billboard, nebulae, dust, storms, particles, weapons,
  shields, thrusters, collision bursts, afterburners, wormholes, FTL, sun
  distortion, and a displaced-rock asteroid field.

### Deterministic procedural generation

- A pure content generator produces stars, planets, asteroids, stations,
  resource nodes, and nebulae from seed, cell, and scale level.
- Existing deterministic paths use integer hashing and avoid architecture
  dependent floating-point noise for authoritative content.
- Generation supports object budgets, timed generation, parallel generation,
  deterministic chunk ordering, LRU caching, upload budgets, LOD
  hysteresis, and sparse player-delta overlays.
- A 7,200-frame flight proof crosses a level-0 cell boundary beyond
  `10¹² m` while matching fresh direct generation byte-for-byte.

### ECS, physics, and audio

- ECS provides transforms, velocity, spatial indexing, instance streaming,
  and inter-starship operations with precision-preserving fixed steps.
- Physics uses Jolt through camera-relative islands, CCD, raycasts, sensor
  triggers, deferred contact events, and deterministic fixed-stepping.
- Audio is integrated behind the public facade with per-bus gain, positional
  audio, Doppler, ECS sync, thread-safe triggers, and a headless
  `Backend::Silent` mode.

### AI behavior

- AI components provide faction, health, navigation target, route state, and
  behavior state.
- Perception finds nearby hostiles through the spatial index.
- Steering supports seek, arrival braking, acceleration limits, separation,
  and heading alignment.
- Behavior controls Patrol, Transit, Docking, Engaging, Evading, and
  Retreating states with hysteresis and timers.
- Traffic routes assign deterministic dock slots and lane points.
- NPC ships couple to the physics island and remain precise beyond
  `10¹² m`.
- Combat uses range, cooldown, and Jolt raycast line-of-sight checks, and
  emits damage/fire events.
- A staggered think schedule bounds per-tick AI cost; a deterministic
  50-ship, 600-frame exit proof verifies repeatability at 60 fps.

### Measurement and testing

- Benchmarks use percentile statistics, versioned JSON, GPU timestamps,
  CPU frame timing, VMA snapshots, latency/throughput modes, and a compare
  tool.
- Test coverage includes CPU suites, headless GPU suites, golden images,
  public API compilation, leakage gates, and deterministic multi-core
  parity checks.

## Current limitations

- M15 gameplay, M16 HUD/UI, M17 asset loading, and M18 persistence remain
  future work.
- Planet terrain-height displacement and different-LOD T-junction skirts are
  still TODO.
- HDR bloom, true volumetric raymarching, GPU particle buffers, and GPU
  asteroid culling are follow-up polish.
- The formal baseline-to-budget comparison table, profiler integration, and
  some parallel-speedup benchmarks remain deferred.

## License

Copyright (c) 2026 Ray Hague. All rights reserved.

This repository is published for viewing and reference only. No license is
granted to use, copy, modify, merge, publish, distribute, sublicense, or sell
this software or its documentation without prior written permission from the
copyright holder. See [LICENSE](LICENSE).

Third-party components under `third_party/` are governed by their own license
terms (for example, Vulkan Memory Allocator is licensed under the MIT License).
