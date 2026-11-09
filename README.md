![StarForge - Space game focused library built on Vulkan](screenshots/Starforge_Banner.png)

# STARFORGE

Space-focused rendering library built on Vulkan.

## Project Vision

STARFORGE is a specialized rendering library designed for
large-scale 3D space games and simulations.

Its purpose is to provide a developer-friendly interface
optimized for:

- Massive planetary environments
- Large-scale coordinate systems
- Asteroid fields
- Spacecraft and fleet rendering
- GPU-driven rendering
- Volumetric space environments
- Advanced space combat effects

See [docs/VISION.md](docs/VISION.md) for goals and non-goals.

## Technology

- C++26
- Vulkan 1.3 minimum, 1.4 preferred
- Vulkan Memory Allocator (VMA)
- GLM (wrapped behind STARFORGE math aliases)
- Slang shaders
- SDL3
- CMake
- Ninja
- Clang

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
    src/content/         Deterministic procedural content generation and deltas
    src/jobs/            Job scheduler, dependencies, scratch memory, deferred deletion
    src/stats/           Shared statistics helpers
    src/platform/        SDL3 window, surface, and event handling
    src/vulkan/          Private Vulkan backend (instance, device, swapchain, renderer)
    src/gpu_resources/   VMA allocator, buffers, images, transfer, compute, culling
    src/planets/         Cubed-sphere and quadtree planet systems, meshes, terrain, atmosphere
    src/environment/     Internal star, nebula, dust, storm, wormhole, FTL, combat,
                         ship, asteroid-field, and other effect renderers
    src/bench/           Benchmark harness, scene/workload generation, results, and compare tool
    apps/sandbox/        Thin host application for experiments
    benchmarks/          Machine-readable frame-time budgets and baseline data
    tests/               CPU unit tests (T1), headless GPU tests (T2/T3), and golden images
    shaders/             Slang shader sources (compute/ and graphics/)
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

## Current Development Status

Milestone M0: Project Foundation

- [x] Repository structure
- [x] C++26 configuration
- [x] Vulkan dependency
- [x] SDL3 dependency
- [x] Slang integration
- [x] Vulkan instance initialization verified on target hardware (RX 7900 XTX)

Milestone M1: Present a Frame

- [x] 1.1 Library / sandbox / tests structure
- [x] 1.2 Window separated from main
- [x] 1.3 VulkanContext (validation, GPU selection, capabilities, feature override)
- [x] 1.4 Headless VulkanDevice
- [x] 1.5 GPU resources (VMA, buffers, upload)
- [x] 1.6 Compute dispatch verified against a CPU reference
- [x] 1.7 VulkanSwapchain with resize recreation
- [x] 1.8 Frame loop and cleared frame on screen

Milestone M2: Precision and World Addressing (CPU only)

- [x] 2.0 World scale requirements note (agreed)
- [x] 2.1 Position representation characterization (float, double, int64 mm, cell+offset)
- [x] 2.2 Math library (GLM behind STARFORGE aliases) + convention tests
- [x] 2.3 WorldPosition (int64 cell + double offset, floor semantics, cell hash)
- [x] 2.4 Camera-relative conversion (double compose, float cast, large-object jitter test)
- [x] 2.5 Translation-invariance property tests (random, negative, boundaries, near-overflow)

Milestone M3: Geometry and Precision on the GPU

- [x] 3.0 Decision note (ADR-002)
- [x] 3.1a Image RAII wrapper + readback test
- [x] 3.1b Depth image + format fallback test
- [x] 3.1c Offscreen render target (color + depth) test
- [x] 3.2a Multi-stage shader CMake support
- [x] 3.2b BDA geometry infrastructure
- [x] 3.2c Graphics pipeline helper + triangle render test
- [x] 3.2d Indexed cube with per-face colors
- [x] 3.3a Reverse-Z projection math
- [x] 3.3b Depth buffer integration + ordering test
- [x] 3.4a Render-object submission
- [x] 3.4b Invariance proof (T1 + T3)
- [x] 3.4c Naive control error table
- [x] 3.5 Sandbox integration

Milestone M4: Measure (largely complete)

The benchmark methodology, frame-time budgets, and hardware scope are
documented in [ADR-004](docs/decisions/004-performance-budgets.md), with
machine-readable allocations in `benchmarks/budgets.json`. A formal
committed baseline-versus-budget table remains follow-up work.
- [x] 4.0 Decision notes: benchmark methodology, timing modes, noise policy, metrics, JSON schema, workload profiles, budgets (Tracy remains deferred)
- [x] 4.1a Statistics library (percentiles) + T1 tests
- [x] 4.1b JSON writer with versioned schema + T1 tests
- [x] 4.1c GPU timestamps + named scopes + T2 tests
- [x] 4.1d CPU frame timing (recording, submission, preparation, interval)
- [x] 4.1e VMA memory statistics
- [x] 4.1f Timestamp validation and availability
- [x] 4.2a Headless harness (latency + throughput modes)
- [x] 4.2b Warm-up and benchmark phases
- [x] 4.2c Noise-floor measurement
- [x] 4.2d Latency and throughput modes
- [x] 4.2e Benchmark environment reporting
- [x] 4.2f Compare tool + T1 tests
- [x] 4.3a Scene spec + scene generation + frustum culling + tests
- [x] 4.3b Naive + competent CPU-driven baselines (infrastructure ready, benchmark test verifies harness runs)
- [x] 4.3c Sweep (10k/100k/1M × ~100%/~10%/~1% visible) (infrastructure ready, sweep verified via test)
- [x] 4.3d Per-machine baseline files + re-baselining procedure (infrastructure ready, baseline files generatable)
- [x] 4.4a Frame-time target and budget document (ADR-004 and benchmarks/budgets.json)
- [ ] 4.4b Baseline-to-budget comparison (compare tool reports budget status, but no committed comparison table yet)
- [x] 4.4c Hardware scope documentation (RX 7900 XTX development target)

Milestone M5.5: Job System & Multi-Threading Foundation (largely complete)

- [x] 5.5.1a Thread pool and task graph -- centralized queue with work-helping; Chase-Lev work-stealing upgrade pending
- [x] 5.5.1b Task graph (counter-based dependencies, continuations) -- dependencies working with race fixes
- [x] 5.5.1c Thread-local scratch linear allocators -- thread_local chunked blocks, no pointer invalidation
- [x] 5.5.1d CPU epoch/frame-based deferred deletion queue -- retire/collectRetired with safe deletion
- [ ] 5.5.1e Profiler integration (Tracy/Superluminal) -- deferred to when Tracy is integrated
- [x] 5.5.2a DeltaStore thread-safety (shared_mutex; apply unique_lock, reads shared_lock)
- [x] 5.5.2b VMA multi-threaded allocation validation (VMA is thread-safe by default)
- [x] 5.5.2c WorldPosition and Math purity verification (value types, no shared state)
- [x] 5.5.3a Per-thread, per-frame-in-flight command pools (ThreadedCommandPools)
- [x] 5.5.3b Secondary command buffers (Dynamic Rendering inheritance) -- completed during M7.2d
- [x] 5.5.3c GPU-timeline deferred resource retirement queue -- completed during M7.2d
- [x] 5.5.3d Asynchronous staging ring-buffer uploader (StagingRingBuffer)
- [x] 5.5.4a Pure functional cell generation jobs (generateContentParallel, batched, span-based)
- [x] 5.5.4b Isolated runtime TLS RNG (thread_local scratch in job scheduler)
- [x] 5.5.4c Lock-free chunk collection buffer (pre-allocated result vector)
- [x] 5.5.4d Multi-threaded long-flight simulation (generateContentParallel)
- [x] 5.5.5a Parallel heightfield evaluation (heightFractalBatch, job-based, refactored per Google review)
- [x] 5.5.5b Parallel atmosphere LUT generation -- completed during M7.4a
- [x] 5.5.5c Parallel quadtree LOD traversal -- completed during M7.1b
- [ ] 5.5.5d Parallel secondary command recording -- deferred follow-up
- [x] 5.5.6a ThreadSanitizer build (with driver suppressions) -- build config documented
- [x] 5.5.6b Multi-core bit-exact determinism test (content + terrain)
- [ ] 5.5.6c Scaling benchmarks (1/2/4/8/16 threads) -- deferred to profiling pass
- [x] 5.5.6d High-concurrency stress test (1000 tasks)

Milestone M5: GPU-Driven Rendering (Partial)

- [x] 5.1 Test Suite Decoupling & CMake Fixes (partial)
- [x] 5.2 Frustum & Math Normalization Fixes (partial)
- [x] 5.3 Winding Order & Viewport Correction
- [x] 5.4 Vulkan RAII & Image Abstraction
- [x] 5.5 Compute & Timestamp Test Hardening (partial)
- [x] 5.6 Scene Data Layout & Double-Buffered Storage
- [x] 5.7 Slang Culling Compute Shader
- [x] 5.8 Render Pass Integration & Barriers
- [x] 5.9 T2 Parity Test (Compute vs. CPU Reference)
- [ ] 5.10 Feature Fallback Verification (partial)
- [ ] 5.11 1M Instance Scaling & Baseline Comparison (partial)

Milestone M6: Deterministic Exploration

- [x] 6.1.1 Content Generation API
- [x] 6.1.2 Integer Hash Functions
- [x] 6.1.3 Scale Level Traversal
- [x] 6.2.1 Star System Generation
- [x] 6.2.2 Asteroid Field Generation
- [x] 6.2.3 Station/Outpost Generation
- [x] 6.3.1 Delta Storage API
- [x] 6.3.2 Delta Application
- [x] 6.4.1 Long Flight Simulation (correctness)
- [x] 6.4.2 Translation-Invariance at Maximum Extent

Milestone M7: Planets (complete)

- [x] Double-precision cubed-sphere mapping with round-trip and boundary tests
- [x] CPU quadtree LOD with screen-space error, hysteresis, sibling 2:1 checks, and
      bounded leaf counts
- [x] Parallel quadtree traversal with deterministic, bit-identical leaves
- [x] Patch mesh generation: RTC vertices, normals, UVs, indexed geometry, analytical
      double-precision bounding spheres, and outward winding
- [x] GPU planet patch rendering through BDA buffers, Slang shaders, a depth-tested
      dynamic-rendering pipeline, secondary command buffers, and a GPU retirement queue
- [x] Headless descent render at multiple altitudes and cross-face seam coincidence checks
- [x] Fractal terrain height generation and parallel heightfield evaluation
- [x] Atmosphere single-scattering LUT generation (Rayleigh/Mie, separate scale heights,
      per-channel transmittance, normalized output)
- [x] Atmosphere shell rendering with BDA LUT sampling and limb-brightening verification

Current limitations recorded in the roadmap: terrain-height displacement is not yet
applied to patch meshes, different-LOD T-junction skirts remain TODO, and several
parallel-speedup validations are still deferred to benchmark passes.

Milestone M8: Environment and Effects (complete)

- [x] World-locked procedural starfield with power-law brightness distribution and
      projectable sun billboard
- [x] FBM volumetric-look nebula and dust passes with inverse-camera directional sampling
- [x] Particles, weapons, shields, thrusters, collision shockwaves, and afterburners
- [x] Sun distortion, wormhole, FTL streak, and storm-cloud passes
- [x] Instanced displaced-rock asteroid field with depth-tested Lambert lighting
- [x] Additive composition across overlay passes
- [x] Starfield golden image and environment render tests
- [x] Frame-time budget smoke test against the ADR-004 6.94 ms interval

Current limitations recorded in the roadmap: HDR bloom post-processing, true
volumetric raymarching, depth-texture refraction/occlusion, GPU particle buffers,
GPU asteroid culling, and additional golden images remain follow-up polish.

Milestone M9: Public API (complete)

The public API under `include/starforge/` is shipped: `Application`,
`Renderer`, `Starfield`, GLM-free math, and `WorldPosition`, all
backend-neutral per ADR-0009. The sandbox is rebuilt using only the
public headers and target, and leakage gates keep Vulkan, VMA, SDL,
and GLM out of the public interface. See
[docs/public-api.md](docs/public-api.md) for the developer guide,
ownership/error rules, and the pre-1.0 stability policy, and
[docs/ROADMAP.md](docs/ROADMAP.md) for the M9 step history.
The next rendering milestone, M9.5 (toon/cel-shaded art direction),
is planned in the roadmap.

Milestone M12: Audio (complete)

miniaudio (v0.11.26) integration, behind the public `starforge::Audio`
facade: per-bus playback, 3D positional audio with Doppler, ECS
emitter/listener sync, and deferred one-shot triggers over a thread-safe
queue — all camera-relative. `Backend::Silent` runs headless on the null
backend. M12 exit proof: spatial audio attenuates with distance and
Doppler sweeps the flyby, both exercised by the sandbox.

## License

Copyright (c) 2026 Ray Hague. All rights reserved.

This repository is published for viewing and reference only. No license is
granted to use, copy, modify, merge, publish, distribute, sublicense, or sell
this software or its documentation without prior written permission from the
copyright holder. See [LICENSE](LICENSE).

Third-party components under `third_party/` are governed by their own license
terms (for example, Vulkan Memory Allocator is licensed under the MIT License).
