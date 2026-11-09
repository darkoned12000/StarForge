# STARFORGE Architecture

This document describes how the code is organized and how each piece
is verified. The order of work and implementation status live in
[ROADMAP.md](ROADMAP.md). `docs/VISION.md` is the authoritative
definition of what STARFORGE is; this document describes how it is
built. It must not maintain a competing status claim.

STARFORGE is a **rendering library** on top of Vulkan (see
`docs/VISION.md`). M10+ game systems (ECS, physics, audio, AI,
procedural content, gameplay) are a **game layer** built on top of
the public API. They live in the same repository but are not part of
the rendering library's public surface.

Guiding idea: **every vision goal gets a proof**, a sandbox scene or
test with a measurable pass criterion, and every subsystem declares how
it is verified.

## Layering

Lower layers never depend on higher ones.

    core / math / spatial        CPU only, no Vulkan
    jobs                         threading foundation (no Vulkan)
    vulkan / gpu_resources       private backend: device, allocator,
                                  buffers/images, pipelines, culling,
                                  staging, retirement
        ├─ planets               quadtree LOD, cubed sphere, atmosphere
        └─ environment           starfield, nebula, dust, effects,
                                  asteroid field, toon material
    platform / public_api        SDL3 window + backend-neutral
                                  facades (include/starforge/)
    game layer (M10+)            ecs · physics · audio · ai · content
                                  (see below)

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
- **Camera-relative single-cast rule.** Absolute astronomical positions
  are never uploaded to the GPU or handed to Jolt/miniaudio. Callers
  convert `WorldPosition` → camera-relative float32 exactly once.
  Physics, audio, and rendering share this rule.

Accepted decisions and intentionally open questions are tracked in
[`docs/decisions/`](decisions/README.md). ADR-0005 (math), ADR-0006
(world position), ADR-0009 (public API contract), ADR-0010 (audio),
ADR-0011 (procedural pipeline), ADR-0012 (noise policy), ADR-0013
(AI components) are accepted.

## Current implementation (M1–M14 complete; M15.0 next)

M1–M14 are complete. The build provides the effective
rendering library plus the game layer (see ROADMAP for status):

- `starforge_core`: logging, version.
- `starforge_math`: GLM behind STARFORGE aliases; Vulkan clip-space
  perspective helpers (reverse-Z, infinite far).
- `starforge_spatial`: `WorldPosition` (int64 cell + double offset,
  FNV-1a cell hash) and camera-relative composition (+ large-object case).
- `starforge_jobs`: job scheduler with task graph, worker pools,
  scratch allocators, deferred reclamation (M5.5).
- `starforge_gpu`: VMA allocator, buffers/images, uploader, compute
  pipelines, BDA, GPU culling, timestamp/statistics, bench harness
  (M1, M3–M5).
- `starforge_vulkan`: instance, device, swapchain, frame loop, pipelines,
  retirement queues, threaded command pools (M1, M5.5).
- `starforge_planets`: quadtree LOD, cubed-sphere projection,
  patch GPU pipeline, atmosphere LUT + shell (M7). Terrain-height
  displacement is still TODO (mesh is sphere today); see M7 residual.
- `starforge_environment`: starfield, nebula, dust, particles, weapons,
  shields, thruster, FTL, wormhole, storm, asteroid field, toon
  material + outline pass (M8, M9.5).
- `starforge_public_api`: backend-neutral facades — `Application`,
  `Renderer`, `Starfield`, `Toon`, `Audio`, math/world (M9, M9.5, M12.5).
- `starforge_ecs`: Flecs world, `Transform`/`Velocity`, spatial index,
  ECS → GPU instance stream (M10).
- `starforge_physics`: Jolt in a camera-relative island, Jolt→scheduler
  adapter, contact queue, CCD/raycast/sensors (M11).
- `starforge_audio`: miniaudio backend, sound banks, buses,
  camera-relative 3D spatialization, ECS bridge (M12).
- `starforge_content`: deterministic `generate(seed, cell, level)`,
  traversal hysteresis, budget enforcement, raw store, delta overlay
  toon-LOD policy (M6, M13).
- `starforge_ai`: steering, perception, FSM, traffic, combat, scheduled
  think (M14).
- `starforge_bench` / `starforge_stats`: harness, JSON, percentiles,
  workload scene, compare (M4).
- `starforge_sandbox`: public-API example (starfield + toon cube +
  M12 audio flyby). No other apps exist yet — `apps/game/` (First
  Flight demo) is planned for M15.0 and does not exist.
- `starforge_tests` (T1–T5, leakage gates) and `starforge_gpu_tests`
  (labelled `gpu`, `SKIP_RETURN_CODE 77`).

Renderer/render-graph: there is no separate `src/renderer/` or
`src/render_graph/` library. Orchestration lives in `starforge_vulkan`
(device/swapchain/frame sync) + `starforge_gpu` (pipelines, culling,
staging) + `starforge_environment` (scene passes). The render-graph
abstraction was deferred until manual barrier management becomes
error-prone (see below).

## Build Targets

| Target | Path | Status | Milestone / Purpose |
|--------|------|--------|---------------------|
| `starforge_core` | `src/core/` | present | logging, version |
| `starforge_math` | `src/math/` | present | vector/matrix types (GLM behind aliases) |
| `starforge_spatial` | `src/spatial/` | present | world positions, camera-relative math |
| `starforge_jobs` | `src/jobs/` | present | M5.5 job scheduler, task graph, scratch allocators |
| `starforge_platform` | `src/platform/` | present | SDL3 window, surface, events |
| `starforge_vulkan` / `starforge_vulkan_core` | `src/vulkan/` | present | instance/device/swapchain/frame + loader-independent core |
| `starforge_gpu` | `src/gpu_resources/` | present | VMA allocator, buffers/images, upload, pipelines, culling |
| `starforge_planets` | `src/planets/` | present | M7 quadtree, cubed sphere, patch pipeline, atmosphere |
| `starforge_environment` | `src/environment/` | present | M8 effects + M9.5 toon (starfield/nebula/asteroids/weapons) |
| `starforge_stats` | `src/stats/` | present | M4 percentiles/summary |
| `starforge_bench` | `src/bench/` (+ `apps/bench/` harness — not separately built) | present | M4 schema, harness, JSON, workload, compare |
| `starforge_content` | `src/content/` | present | M6/M13 generation, traversal, cache, deltas |
| `starforge_ecs` | `src/ecs/` | present | M10 Flecs world, Transform/Velocity, spatial index, instance stream |
| `starforge_physics` | `src/physics/` | present | M11 Jolt island, contact queue, cast/sensor |
| `starforge_audio` | `src/audio/` | present | M12 miniaudio, banks/buses, spatialization, ECS bridge |
| `starforge_ai` | `src/ai/` | present | M14 steering, perception, FSM, traffic, combat, scheduler |
| `starforge_public_api` | `src/public_api/` + `include/starforge/` | present | M9 opaque facades (Application/Renderer/Starfield) + M9.5 Toon + M12.5 Audio |
| `starforge` | (interface) | present | umbrella linking all above |
| `starforge_sandbox` | `apps/sandbox/` | present | public-API example (links public API only) |
| `starforge_tests` / `starforge_gpu_tests` | `tests/` | present | T1 / T2–T4 (label `gpu`) |
| `apps/game/` (`First Flight`) | `apps/game/` | planned | M15.0 player demo — not yet built |

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
- **Decisions:** significant choices are recorded in
  `docs/decisions/NNN-title.md` with alternatives considered.
- **Internal include root:** `src/`, e.g.
  `#include "vulkan/vulkan_context.hpp"`.
- **Threading:** job-system workers are for CPU work. Vulkan pools are
  per-thread, per-frame (no concurrent use). `vkQueueSubmit`/
  `vkQueuePresentKHR` need external sync. Never mutate Flecs from
  Jolt callbacks or the audio mixing thread — use deferred queues
  flushed on the main thread (M11 `ContactQueue`, M12 `TriggerQueue`
  pattern). Job-system TLS RNG is for runtime effects only (audio
  pitch, particle jitter); content generation uses a per-task
  `DeterministicRng` (M5.5).

## Test Tiers

| Tier | What | Example |
|------|------|---------|
| T1 | CPU unit tests (doctest) | Coordinate round-trips, LOD, AI FSM |
| T2 | Headless GPU: run work, read back, compare to CPU reference | Compute culling, BDA, retirement |
| T3 | Offscreen render, golden-image compare, zero validation errors | Same scene at two origins, toon readback |
| T4 | Benchmark, results saved as JSON | 100k instances, CPU vs GPU |
| T5 | Manual sandbox checklist | Resize, minimize, shutdown |

### T5 Manual Checklist (Wayland / X11)

The sandbox must be run and verified on both backends. Set
`SDL_VIDEODRIVER=wayland` or `SDL_VIDEODRIVER=x11` to force a
specific backend.

- [ ] Window opens; SDL logs the selected video driver (`wayland` or `x11`).
- [ ] Resize triggers swapchain recreation (logged, no validation errors).
- [ ] Minimize / restore triggers swapchain recreation (logged, no validation errors).
  - On Wayland, `SDL_WINDOW_HIDDEN` is treated as minimized.
- [ ] HiDPI scaling produces correct framebuffer size (no blurry or oversized output).
- [ ] Close window shuts down cleanly with zero validation errors.
- [ ] (Wayland only) Window decorations are present (libdecor or server-side).
- [ ] (Wayland only) Fractional scaling does not cause 0×0 swapchain extent.

Notes:

- T2/T3 tests are labeled `gpu` in CTest and skip cleanly (exit code 77)
  when no suitable GPU is present.
- Every T2–T4 run asserts zero validation errors after teardown.
- T4 results are only comparable within the same GPU, driver, and build
  type; the JSON records all three.

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

`include/starforge/` (built by `src/public_api/`).

Developer-facing interfaces. Must not expose Vulkan, VMA, SDL, GLM,
or miniaudio types. Shaped from working vertical slices (ADR-0009):
only abstractions exercised by an end-to-end example are public.
Enforced by `public_leakage_scan` + `public_consumer_compile` gates.

- `Application` — window/lifecycle only.
- `Renderer` — swapchain, frame recording callbacks, presentation.
- `Starfield` / `Toon` — domain facades (bundled shaders, scene
  parameters only). Other environment effects stay internal until a
  public consumer justifies them.
- `Audio` — facade over miniaudio (no `ma_*` in public headers).
- `starforge::math` / `starforge::WorldPosition` — GLM-free value
  types and camera-relative helpers (bridged via raw-data bridges so
  public and internal TUs never include both).

Scene-submission API (`scene.add(...)` / `renderer.render(scene)`)
is deferred until a consumer needs it.

## Core / Platform / Math / Spatial

See `src/core/`, `src/platform/`, `src/math/`, `src/spatial/`.

- **Purpose:** logging/version; SDL3 window; vectors/matrices;
  world positions accurate across astronomical distances.
- **Depends on:** nothing (math/spatial are CPU-only).
- **Verified by:** T1 (precision demo quantifies the naive-float error).
- **Settled (M2):** 64-bit cell (2^20 m) + double offset; GLM behind
  aliases (ADR-0005/0006). Sub-mm at any distance up to ~9.7e24 m.
- **Metric:** near-camera relative positions accurate to <1 mm
  regardless of distance from origin.
- **Floating-origin rebase:** deferred until a float-based consumer
  needs it (physics already uses camera-relative islands).

## Jobs

`src/jobs/`

- **Purpose:** task graph + worker pool + scratch allocators +
  deferred reclamation. All parallel work goes through here.
- **Depends on:** core only.
- **Verified by:** T1 (determinism, TSan, 1000-task stress).
- **Metric:** bit-identical output vs sequential at any thread count;
  4x+ on 4 cores for embarrassingly parallel work.

## Vulkan Backend + GPU Resources

`src/vulkan/`, `src/gpu_resources/`

- **Purpose:** instance, GPU selection, device (headless-capable),
  swapchain, frame sync; VMA allocator, buffers/images, upload,
  pipelines (dynamic rendering, synchronization2), BDA, GPU culling,
  retirement queues, timestamp queries.
- **Depends on:** core, platform.
- **Verified by:** T1 (pure logic), T2 (readback vs CPU), T3
  (goldens, zero validation), T5 (swapchain).
- **Requirements:** Vulkan 1.3 min (dynamic rendering, sync2, BDA).
  Optional features (bindless, draw-indirect-count, mesh shaders) are
  in `GpuCapabilities` and need fallbacks before anything depends on
  them. `STARFORGE_DISABLE_FEATURES` forces fallbacks.
- **Metric:** M4 baselines + 1M-instance GPU-driven beat over competent
  CPU baseline; frame budget ADR-004 (6.94 ms @ 144 Hz, dev machine).
- **Decision:** VMA (ADR-0004). volk deferred.

## Planets

`src/planets/`

- **Purpose:** quadtree LOD, cubed-sphere projection, GPU patch
  pipeline, atmosphere LUT + shell.
- **Depends on:** renderer/gpu, spatial, jobs.
- **Verified by:** T1 (LOD, seam coincidence), T3 (patch render,
  descent), T4 (patch counts).
- **Metric:** orbit-to-surface descent, bounded patches, no cracks
  (cross-face <1 m; T-junction skirts TODO).
- **TODO:** terrain-height displacement (mesh is sphere today) +
  retirement/parallel speedup validations — see M7 residual.

## Environment (incl. Toon)

`src/environment/`

- **Purpose:** starfield/sun, nebula/dust FBM, storm, particles,
  weapons/shields/thruster/FTL/wormhole/collision, instanced
  asteroid field, toon material (quantized bands, stepped specular,
  rim, inverted-hull outlines).
- **Depends on:** gpu, spatial.
- **Verified by:** T2 (creation), T3 (pixel readback, goldens for
  starfield + toon), T4 (budget).
- **Fallback:** mesh shaders only behind `GpuCapabilities.meshShader`;
  currently no mesh/task shaders are used.
- **Material model (M9.5 roadmap):** reusable capabilities combined
  per material class, not seven separate shaders. Backgrounds stay
  soft vs. outlined hero objects. Production showcase (9.5.6) is the
  M19 gate, not the cube proof.

## Content (procedural streaming)

`src/content/`

- **Purpose:** deterministic `generate(seed, cell, level)` (integer-hash,
  no FP noise for authoritative state — ADR-0011/0012), traversal
  hysteresis with toon LOD rule, generation/upload budgets, bounded
  raw store (LRU), sparse delta overlay (`ApplyDelta` read-time).
- **Depends on:** spatial, jobs, ecs (for instantiation helpers).
- **Verified by:** T1 (bit-exact across runs/machines, flythrough),
  T3 (headless render suites stay clean).
- **Metric:** 2-min flythrough at 1,000 m/s with precision held;
  >1e12 m flight matches fresh generation byte-for-byte.

## ECS

`src/ecs/`

- **Purpose:** Flecs world; `Transform` (WorldPosition + orientation)
  + `Velocity`; cell-hash spatial index; ECS → GPU instance stream.
- **Depends on:** spatial, gpu.
- **Verified by:** T1 (precision, queries), T2 (instances render).
- **Metric:** 10k entities step+collect+render p95 under ADR-004.

## Physics

`src/physics/`

- **Purpose:** Jolt in a camera-relative island (±5–10 km bubble),
  ECS bridge (pre/post sync), deferred contact queue, CCD/raycast/
  sensors. Jolt jobs map onto `starforge::jobs`.
- **Depends on:** ecs.
- **Verified by:** T1 (determinism, CCD, raycast), T2 (field proof).
- **Metric:** ship-vs-asteroid field, no tunneling at 1 km/s,
  p95 step+sync ≤2 ms.
- **TODO:** island-boundary crossing stress test (M11.6).

## Audio

`src/audio/`

- **Purpose:** miniaudio context (null backend for tests), sound
  banks/buses, procedural tone fixtures, CPU spatialization
  reference, camera-relative emitter/listener sync, trigger queue.
- **Depends on:** ecs.
- **Verified by:** T1 (attenuation/pan/Doppler, sync, triggers).
- **Metric:** distance attenuation + Doppler on flyby; no audio
  types in public headers (leakage gate).

## AI

`src/ai/`

- **Purpose:** components (`AiAgent`, `Faction`, `NavigationTarget`,
  `Health`), perception, steering, FSM (Patrol/Transit/Docking/
  Engage/Evade/Retreat), traffic + dock slots, combat (raycast LOS,
  cooldown/damage events), timer-based scheduler.
- **Depends on:** ecs, physics.
- **Verified by:** T1 (FSM, traffic repeatability, perception),
  T4 (50-ship perf + determinism).
- **Metric:** 600-frame 50-ship war sim: identical final positions,
  engagements + damage events, p95 inside 60 fps budget.
- **Follow-ups (M14):** wreck resolution (M15), projectiles, finer
  perception grid, station approach side, up-vector control.

## Renderer orchestration (no separate render-graph library)

Renderer orchestration lives in `starforge_vulkan` + `starforge_gpu`
+ `starforge_environment`: hand-written pass sequences with dynamic
rendering and `synchronization2`. A render graph will be introduced
only when manual barrier management becomes error-prone. Secondary
command buffers with dynamic-rendering inheritance and GPU retirement
queues already exist for the planet pipeline (M5.5/M7.2d).
