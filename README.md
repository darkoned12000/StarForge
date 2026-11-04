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

    include/starforge/   Public, developer-facing headers (Vulkan-free)
    src/core/            Logging, version, foundational utilities
    src/math/            Vector, matrix, and Vulkan clip-space projection helpers
    src/spatial/         WorldPosition (int64 cell + double offset) and camera-relative math
    src/platform/        SDL3 window, surface, and event handling
    src/vulkan/          Private Vulkan backend (instance, device, swapchain, frame loop)
    src/gpu_resources/   VMA allocator, buffers, staging upload, compute pipeline
    src/bench/           Benchmark result schema, JSON I/O, and metadata
    apps/sandbox/        Thin host application for experiments
    tests/               CPU unit tests (T1) and headless GPU tests (T2/T3)
    shaders/             Slang shader sources
    third_party/         Vendored third-party libraries (VMA)
    cmake/               Build helper modules
    docs/                Vision, architecture, roadmap, decisions, AI instructions

## Architecture

Vulkan is the initial GPU backend.

The public STARFORGE API must remain independent
of Vulkan implementation details.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/ROADMAP.md](docs/ROADMAP.md).
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

Milestone M4: Measure

- [ ] 4.0 Decision notes: Tracy, benchmark methodology, timing modes, noise policy, metrics, JSON schema, workload profiles, budgets
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
- [ ] 4.4a Frame-time target and budget document
- [ ] 4.4b Baseline-to-budget comparison
- [ ] 4.4c Hardware scope documentation

See [docs/ROADMAP.md](docs/ROADMAP.md) for details.

## License

Copyright (c) 2026 Ray Hague. All rights reserved.

This repository is published for viewing and reference only. No license is
granted to use, copy, modify, merge, publish, distribute, sublicense, or sell
this software or its documentation without prior written permission from the
copyright holder. See [LICENSE](LICENSE).

Third-party components under `third_party/` are governed by their own license
terms (for example, Vulkan Memory Allocator is licensed under the MIT License).
