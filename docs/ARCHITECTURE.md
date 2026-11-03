# STARFORGE Architecture

This document describes how the code is organized and how each piece
is verified. The order of work lives in [ROADMAP.md](ROADMAP.md).

Guiding idea: **every vision goal gets a proof**, a sandbox scene or
test with a measurable pass criterion, and every subsystem declares how
it is verified.

## Layering

Lower layers never depend on higher ones.

    core
     ├─ math / spatial            CPU only, no Vulkan
     └─ vulkan                    private backend
         └─ gpu_resources         allocator, buffers, images, upload
             ├─ render_graph      passes, dependencies, barriers
             │   └─ renderer      orchestration
             └─ gpu_scene         culling, draw generation
                 └─ planets, environment, effects
     platform                     SDL3 window (depends on core only)
     public API (include/)        last; wraps a working vertical slice

Rules that follow from this:

- **Hard math stays Vulkan-free.** Precision, coordinate hierarchies,
  LOD selection and culling math live on the CPU side and are
  unit-tested without a GPU.
- **CPU reference first.** Every GPU-driven algorithm (culling, LOD,
  patch generation) is first written as a plain CPU function. The
  compute shader is tested against it.
- **Headless-capable device.** `VulkanDevice` works without a window;
  the swapchain is an optional layer on top. This enables GPU tests
  without a display.
- **No RHI.** No generic backend abstraction until a second backend
  exists.

Accepted decisions and intentionally open questions are tracked in
[`docs/decisions/`](decisions/README.md). In particular, the math library and
world-position representation are not settled yet; the M2 work must evaluate
them before spatial code depends on either choice.

## Current implementation (M1 complete)

The architecture below describes the intended subsystem boundaries and
verification contracts. It is not a claim that every listed subsystem is
implemented. The current build provides:

- `starforge_core`: logging and version information.
- `starforge_platform`: an RAII SDL3 window, event polling, framebuffer-size
  queries, Vulkan instance-extension discovery, and surface creation.
- `starforge_vulkan`: Vulkan instance creation, validation setup with error and
  warning counters, physical-device selection, capability reporting,
  optional-feature overrides, headless-capable `VulkanDevice`, `VulkanSwapchain`
  with format/present-mode/extent selection and resize recreation, and
  `FrameRenderer` with frames-in-flight synchronization and dynamic rendering
  clear.
- `starforge_gpu`: VMA-backed `Allocator`, move-only `Buffer` with separate
  usage and memory placement, `Uploader` with `synchronization2` barriers, and
  `ComputePipeline` for one-shot Slang SPIR-V compute dispatches.
- `starforge_sandbox`: window/event-loop host that presents cleared frames and
  recreates the swapchain on resize/minimize.
- `starforge_tests` (T1 CPU tests) and `starforge_gpu_tests` (T2/T3 headless
  GPU tests, labeled `gpu` in CTest with `SKIP_RETURN_CODE 77`).

The renderer, render-graph, spatial, and scene directories remain roadmap
structure for M2+.

## Build Targets

| Target               | Path               | Status  | Purpose                                  |
|----------------------|--------------------|---------|------------------------------------------|
| `starforge_core`     | `src/core/`        | present | Logging and version; foundational utilities as needed |
| `starforge_platform` | `src/platform/`    | present | SDL3 window, surface, and event handling |
| `starforge_vulkan`   | `src/vulkan/`      | present | Instance, GPU selection, device, swapchain, frame loop |
| `starforge_gpu`      | `src/gpu_resources/` | present | Allocator (VMA), buffers, upload, compute pipeline |
| `starforge`          | (interface)        | present | Umbrella target applications link to     |
| `starforge_sandbox`  | `apps/sandbox/`    | present | Thin host for experiments                |
| `starforge_tests`    | `tests/`           | present | T1 CPU doctest unit tests                |
| `starforge_gpu_tests`| `tests/`           | present | T2/T3 headless GPU tests (`gpu` label)   |
| `starforge_math`     | `src/math/`        | M2      | Vector/matrix types                      |
| `starforge_spatial`  | `src/spatial/`     | M2      | World positions, camera-relative math    |
| `starforge_bench`    | `apps/bench/`      | M4      | Benchmark harness, JSON output           |

New subsystems are static libraries following the same pattern.

## Conventions

- **Error handling:** fallible construction returns
  `std::expected<T, std::string>` via a static `create()`. Exceptions
  are not used for expected runtime failures.
- **Ownership:** every resource is owned by an RAII, move-only type.
  No manual cleanup paths in `main` or in `create()` functions.
- **Logging:** `core/log.hpp` with a subsystem tag.
- **Capabilities:** hardware features are queried once into
  `GpuCapabilities`. Systems branch on it; nothing assumes a feature.
  `STARFORGE_DISABLE_FEATURES` forces fallback paths on capable GPUs.
- **Validation:** the debug messenger counts errors and warnings so
  tests can assert zero after child resources are torn down.
- **Decisions:** significant choices (spatial representation, math
  library, Tracy, allocator) are recorded as short notes in
  `docs/decisions/NNN-title.md` with the alternatives considered. The index
  distinguishes accepted choices from questions intentionally left open.
- **Internal include root:** `src/`, e.g.
  `#include "vulkan/vulkan_context.hpp"`.

## Test Tiers

| Tier | What                                                       | Example                                   |
|------|------------------------------------------------------------|-------------------------------------------|
| T1   | CPU unit tests (doctest)                                   | Coordinate round-trips, LOD selection     |
| T2   | Headless GPU: run work, read back, compare to CPU reference| Compute culling output, buffer round-trip |
| T3   | Offscreen render, golden-image compare, zero validation errors | Same scene rendered at two origins    |
| T4   | Benchmark, results saved as JSON                           | 100k instances, CPU-driven vs GPU-driven  |
| T5   | Manual sandbox checklist                                   | Resize, minimize, shutdown                |

Notes:

- T2 and T3 tests are labeled `gpu` in CTest and skip cleanly (exit code 77)
  when no suitable GPU is present.
- Every T2–T4 run asserts zero validation errors after teardown.
- T4 results are only comparable within the same GPU, driver and
  build type; the JSON records all three.

## Definition of Done (every step)

1. Builds with no compiler warnings.
2. New logic has tests at its declared tier; all tests pass.
3. Zero validation errors for any GPU work.
4. Optional-feature code paths have been run once with the feature
   disabled via `STARFORGE_DISABLE_FEATURES`.
5. Performance-sensitive changes have a recorded benchmark.
6. Docs updated if architecture changed.

## Subsystem Template

Each subsystem section answers: purpose, depends on, verified by
(tier), fallback, and the metric that proves it. If a subsystem cannot
fill these in, it is not ready to be built.

## Public API

`include/starforge/`

Developer-facing interfaces. Must not expose Vulkan types.
Intentionally near-empty until a working vertical slice exists; the API
is shaped by using the internals first.

## Core

`src/core/`

- **Purpose:** logging, version information, and foundational utilities.
  Application-lifecycle responsibilities have not been defined or
  implemented.
- **Depends on:** nothing.
- **Verified by:** T1.

## Platform

`src/platform/`

- **Purpose:** SDL3 window, events, resize and minimize state,
  required instance extensions.
- **Depends on:** core.
- **Verified by:** T5 (needs a display).

## Vulkan Backend

`src/vulkan/`

- **Purpose:** instance, validation, GPU selection, logical device,
  queues, swapchain, and frame synchronization.
- **Depends on:** core.
- **Verified by:** T1 for pure logic (scoring, version, format
  selection); T2/T3 for the device and dynamic rendering clear; T5 for
  the swapchain.
- **Requirements:** Vulkan 1.3, dynamic rendering, synchronization2,
  buffer device address, a graphics queue. 1.4 requested when
  available. Optional (bindless, draw-indirect-count,
  multi-draw-indirect, shaderFloat64, mesh shaders) are reported in
  `GpuCapabilities` and need a fallback before anything depends on
  them.
- **Components:** `VulkanContext`, `VulkanDevice` (headless or surface-capable),
  `VulkanSwapchain`, `FrameRenderer`.

## GPU Resources

`src/gpu_resources/`

- **Purpose:** centralized allocation: VMA allocator, buffers, images,
  staging uploads, compute pipelines, later the bindless tables.
- **Depends on:** vulkan.
- **Verified by:** T2 (write, copy, read back; no leaks at shutdown).
- **Decision:** VMA rather than a hand-rolled allocator (integrated in
  M1.5). See [ADR-0004](decisions/0004-vma-allocator.md).
  volk is optional and deferred.

## Math and Spatial

`src/math/`, `src/spatial/`

- **Purpose:** vector/matrix types; world positions that stay accurate
  across astronomical distances; camera-relative conversion;
  floating-origin rebasing.
- **Depends on:** core only. No Vulkan.
- **Verified by:** T1, with the precision demo quantifying the error
  of the naive approach and the fix.
- **Open decision (M2):** 64-bit integer cell plus float offset versus
  double positions rendered camera-relative. A double has about 1e-4 m
  spacing at 1e12 m but about 1e5 m at 1e21 m, so doubles suit a
  solar system but not a galaxy; the hierarchy in M6 interacts with
  this choice.
- **Metric:** relative positions near the camera stay accurate to
  better than 1 mm regardless of distance from the world origin.

## Renderer and Render Graph

`src/renderer/`, `src/render_graph/`

- **Purpose:** renderer orchestrates frames; the render graph owns
  passes, resource dependencies and barriers.
- **Depends on:** gpu_resources, vulkan.
- **Verified by:** T3 (golden images, zero validation errors).
- **Fallback:** none needed; only dynamic rendering and synchronization2.
- **Note:** start with a hand-written pass sequence. Introduce the
  graph only when manual barrier management becomes error-prone.

## GPU Scene

`src/gpu_scene/`

- **Purpose:** GPU-driven object organization, culling, indirect draw
  generation.
- **Depends on:** gpu_resources, spatial.
- **Verified by:** T2 against the CPU reference culler; T4 against the
  CPU-driven baseline.
- **Fallback:** without `drawIndirectCount`, fixed-size indirect
  buffers; without `bindless`, a bound-descriptor path.
- **Metric:** frame time at 100k and 1M instances versus baseline.

## Planets

`src/planets/`

- **Purpose:** planet geometry, LOD, atmosphere.
- **Depends on:** renderer, gpu_scene, spatial.
- **Verified by:** T1 for CPU quadtree LOD selection; T3 for rendered
  patches; T4 for patch counts and frame time.
- **Metric:** continuous descent from orbit to surface with bounded
  patch count and no visible cracks.

## Environment

`src/environment/`

- **Purpose:** stars, nebulae, dust and other space environments.
- **Depends on:** renderer, gpu_scene.
- **Verified by:** T3, T4.
- **Fallback:** mesh-shader paths only behind
  `GpuCapabilities.meshShader`.

## Effects

`src/effects/`

- **Purpose:** particles, weapons, explosions and shields.
- **Depends on:** renderer, gpu_scene.
- **Verified by:** T3, T4.
