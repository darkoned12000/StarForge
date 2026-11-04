# STARFORGE Roadmap

Principles:

- Every vision goal gets a **proof**: a sandbox scene or test with a
  measurable pass criterion.
- Every step fits in about one session, leaves the build green, and
  states how it is verified (tiers T1–T5 are defined in
  [ARCHITECTURE.md](ARCHITECTURE.md)).
- Plan the next few milestones in detail; later ones stay at goal plus
  proof so the plan does not go stale.
- Vulkan plumbing is bounded (M1). A differentiator is proven in M3.

Every step also satisfies the Definition of Done in ARCHITECTURE.md.

## Vision goals and their proofs

| Vision goal                         | Proof                                                | Milestone |
|-------------------------------------|------------------------------------------------------|-----------|
| Accurate positioning at astronomical distances | Same scene at 0 and 1e12 m renders identically | M2, M3 |
| Unbounded exploration, no loading zones | Content is a deterministic function of position (bit-exact across runs); simulated long flight stays precise | M2, M6 |
| High-performance GPU rendering      | Measured frame times, budgets from a baseline        | M4        |
| Enormous object counts / GPU-driven | 1M instances culled and drawn on GPU, beats baseline | M5        |
| Scalable planetary rendering        | Orbit-to-surface descent, bounded patches            | M7        |
| Advanced space environments         | Star field, nebula, dust at target frame time        | M8        |
| Simple developer-facing API         | Sandbox scene built only through the public API      | M9        |

## M1 – Present a Frame

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 1.1 ✅ | Libraries, sandbox, tests structure | Build | Clean build, tests run |
| 1.2 ✅ | Window separated from `main` | T5 | Window opens, resize logged |
| 1.3 ✅ | `VulkanContext`: instance, validation, debug messenger, GPU selection, capabilities, feature override, driver decode | T1 + run on target GPU | RX 7900 XTX selected, capabilities logged |
| 1.4 ✅ | Headless `VulkanDevice`: logical device with 1.3 features, graphics queue, object naming. Messenger counts validation errors and warnings | T2 | Create and destroy with no window; error count is 0 (warnings are reported and reviewed) |
| 1.5 ✅ | `gpu_resources`: VMA allocator, `Buffer`, staging upload | T2 | 1 MiB GPU roundtrip (CPU → staging → device-local → readback) matches byte-for-byte; no leaks at shutdown; zero validation errors |
| 1.6 ✅ | Compute dispatch: load SPIR-V built by Slang, run a known kernel over N floats | T2 | GPU output equals CPU reference; proves Slang → SPIR-V → GPU |
| 1.7 ✅ | `VulkanSwapchain`: surface, present queue, format and present-mode choice, recreation on resize, minimize and out-of-date | T1 (format and extent selection) + T5 | Resize and minimize checklist passes |
| 1.8 ✅ | Frame loop: command pools, frames in flight, semaphores and fences, cleared frame via dynamic rendering and synchronization2 | T5 + validation | 600 frames incl. resizes with 0 validation errors; clean shutdown |

### 1.5 Subtasks

| Subtask | Deliverable | Verified by | Done when |
|---------|-------------|-------------|-----------|
| 1.5.1 | VMA dependency integration | Build | Pinned VMA release available via build; `starforge_gpu` links VMA and Vulkan |
| 1.5.2 | RAII allocator wrapper (`gpu_resources/allocator.*`) | T1/Build | Allocator created from instance/physical device/device; create/destroy cleanly; no VMA/Vulkan types exposed in public headers |
| 1.5.3 | Move-only `Buffer` wrapper | T1 | Owns `VkBuffer` + `VmaAllocation`; usage and memory placement are separate concepts; supports mapping, flushing, invalidation; move-only |
| 1.5.4 | Immediate transfer submission utility (command pool/buffer, one-time submit, fence) | T2 | Headless copy with proper buffer memory barriers (available/visible); queue family internal to Vulkan backend |
| 1.5.5 | Staging upload and readback test (GPU roundtrip) | T2 | 1 MiB CPU → staging → device-local → readback matches byte-for-byte; deterministic data; uses appropriate memory dependencies |
| 1.5.6 | Allocation accounting and teardown verification | T2 | All test allocations released before allocator destruction; VMA allocation count zero or accounted for; zero validation errors |

**Acceptance requirements (1.5):**
- No SDL window or Vulkan surface required (headless).
- No Vulkan or VMA types exposed through public STARFORGE headers.
- Buffer usage and memory placement are separate; mapping handles non-coherent memory (flush/invalidate).
- Transfer operations use proper Vulkan memory dependencies (not just fence wait).
- Queue/command-pool ownership remains internal to the Vulkan backend.
- Test verifies all 1,048,576 bytes against deterministic CPU-generated source data.
- Validation error count is zero; warnings reported and reviewed.
- Resources destroyed in dependency-safe order (buffers → allocator → device → instance).
- Existing M1.3 and M1.4 tests continue to pass.


Note for 1.6: the kernel in `shaders/compute/test_compute.slang` is
used if it has a checkable output; otherwise replace it with a simple
known kernel (for example `y = a*x + b`).

Exit: a cleared frame on screen, validation-clean, resizing correctly.

## M2 – Precision and World Addressing (CPU only)

Requirements and reasoning: [decisions/001-world-scale-requirements.md](decisions/001-world-scale-requirements.md).

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 2.0 ✅ | Requirements note: required extent, near-camera resolution, determinism, how positions reach a float32 shader, consumers | Review | Note agreed; numbers in it are the acceptance criteria for 2.1–2.5 |
| 2.1 ✅ | Characterization tool: reach and spacing of float, double, int64 millimetres, and int64 cell + double offset, from 1e6 m to beyond 1e24 m. Uses no math library | T1 + generated table | Table committed to the note; test asserts naive float error exceeds a stated bound at each distance (proves the problem) and the chosen candidate meets the requirements |
| 2.2 ✅ | Math library: GLM wrapped behind STARFORGE aliases (see [ADR-0005](decisions/0005-math-library.md)) and **convention tests**: handedness, up axis, column-major layout, Vulkan Y-down clip space, depth 0..1, reverse-Z (near → 1, far → 0) | T1 | Conventions documented and tested. Library internals are not tested |
| 2.3 ✅ | `WorldPosition` (decision note + implementation): power-of-two cells with a local offset, shift-based coarse cell index at any scale (floor semantics), overflow detection, NaN/infinity rejected, stable cell hash | T1 | Round-trip and arithmetic correct at 1e12 m and at the maximum extent; negative coordinates and cell boundaries correct; known cells hash to known values |
| 2.4 ✅ | Camera-relative conversion of positions **and full transforms** (compose in double, cast to float once). Includes the large-object case: planet radius 6.4e6 m, camera near its surface, with a per-object local origin | T1 | Error stays within the bound derived from the representation; the large-object case has no vertex jitter at the surface |
| 2.5 ✅ | Translation-invariance property tests: random inputs, negative coordinates, cell boundaries, near-overflow. Same relative scene shifted by T from 0 to the maximum extent | T1 | Camera-relative results match within the bound for every T |

Deferred (not scheduled): floating-origin rebase. Rendering is
camera-relative per frame, so a rebase is only needed by float-based
simulation (for example physics). Add it when the first such consumer
exists.

Exit: precision and addressing claims demonstrated with numbers,
before any code stores positions.

## M3 – Geometry and Precision on the GPU

M3 proves the central claim: accurate positioning across astronomical distances.
M2 proved the math (CPU side); M3 proves it end-to-end through the GPU pipeline.

### 3.0 Decision note (Review)

Before implementation, resolve these design decisions and record them as ADRs:

- **GPU coordinate system boundary (core design rule):** The CPU owns
  astronomical world coordinates; the GPU owns camera-relative rendering
  coordinates. Absolute astronomical world positions are never uploaded to
  the GPU. World-to-camera-relative conversion is performed using STARFORGE's
  high-precision CPU spatial system. GPU-facing transforms contain only
  camera-relative coordinates. The renderer's camera/projection data contract
  is defined separately from the world-position representation.

- **Camera and mesh data path:** Push constants for per-draw data (camera-
  relative model matrix as 4×4 float = 64 bytes, plus material index as
  uint32 = 4 bytes, totalling 68 bytes — fits within Vulkan's guaranteed
  128-byte minimum push-constant capacity). Buffer device address (BDA) for
  vertex/index data via shader-side geometry access (buffer references in
  shaders, not conventional vertex binding). Required Vulkan features:
  `bufferDeviceAddress`. Required buffer usage:
  `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`. VMA allocator configured with
  `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT` (already enabled in M1.5).
  Device-address alignment requirements respected. Lifetime guarantees: a
  referenced buffer must remain alive while GPU work can access its address.
  BDA is chosen for geometry only; descriptors may still be used for other
  resources later. No world-space floats or separate model/view matrices are
  ever uploaded — the model matrix contains camera-relative translation only.

- **Image format and colour space:** RGBA8 UNORM for offscreen comparison
  (separate from the swapchain's sRGB). Depth: D32_SFLOAT preferred. For
  the astronomical-depth proof, a floating-point depth format is **required**
  — D24_UNORM_S8_UINT is a compatibility fallback for ordinary rendering
  only and does not provide the same reverse-Z precision benefits. Format
  support queried at runtime via `findDepthFormat()`. Selected format is
  logged and recorded in test output.

- **Golden image policy:** PNG via stb_image_write (write) and stb_image
  (read) — both vendored as single headers in `third_party/`. Per-channel
  tolerance plus maximum failing-pixel fraction. Small images (256×256).
  Comparison details: alpha is compared; comparison occurs in the image's
  native space (UNORM, no sRGB conversion for offscreen targets); a pixel
  fails if any channel exceeds tolerance; failing-pixel fraction calculated
  across the entire image; image dimensions must match exactly. Golden-update
  workflow: normal test failure writes actual and diff images to the build
  dir; explicit golden update (`STARFORGE_UPDATE_GOLDENS=ON`) generates
  replacement reference images but never silently overwrites existing
  goldens during an ordinary test run. Anti-aliasing is off for all test
  renders (deterministic pixel output). Note: disabling MSAA does not
  guarantee identical output across GPU vendors, drivers, or shader compiler
  versions — goldens are validated on the target GPU (RX 7900 XTX) and
  tolerances account for minor rasterization differences.

- **Matrix layout:** Slang column-major, matching GLM. Column vectors,
  matrix multiplication order `M * v`. Right-handed coordinate system.
  Vulkan clip-space depth range [0, 1]. Y-inversion occurs in the
  projection matrix (negate Y scale), not via negative-height viewport.
  Set explicitly via Slang compiler flag (`-matrix-layout-column-major`) and
  tested with a non-symmetric matrix verifying both memory layout and
  transformed result.

- **Entry-point naming:** Slang shaders use unique entry-point names per
  stage: `vertexMain` for vertex shaders, `fragmentMain` for fragment
  shaders, `computeMain` for compute shaders. The `starforge_add_shader`
  helper accepts the entry point and stage as arguments. The compute
  dispatch fallback (`main` then `computeMain`) is removed — all shaders use
  `main`-equivalent unique names consistently.

- **Depth buffer and reverse-Z:** Clear depth to 0, compare with `GREATER`,
  viewport depth range 0..1, infinite-far projection. Reverse-Z details
  documented in `math.hpp` (`perspectiveVulkanReverseZ`,
  `perspectiveVulkanReverseZInfinite`). Physical limit: reverse-Z with
  D32_SFLOAT gives ~6e-8 relative depth resolution. At 1e9 m, this resolves
  separations of roughly 60–100 m. This is numerical precision, not a
  physical limit, and the actual value depends on the depth representation
  and projection. D24_UNORM_S8_UINT is a fixed-point format and loses the
  reverse-Z precision benefit at extreme distances.

### 3.1 Image support (T2)

| Subtask | Deliverable | Verified by | Done when |
|---------|-------------|-------------|-----------|
| 3.1a ✅ | `Image` RAII wrapper (`VkImage` + VMA), image views, layout transitions via `synchronization2` (`VkImageMemoryBarrier2`), and image→buffer readback | T2 | Allocate, transition, and read back a 4×4 RGBA8 image headlessly with zero validation errors |
| 3.1b ✅ | Depth image creation with format fallback (`D32_SFLOAT` → `D24_UNORM_S8_UINT` → `D32_SFLOAT_S8_UINT`) | T2 | Depth image created and destroyed cleanly; format logged and verified |
| 3.1c ✅ | Offscreen render target (color + depth) with `VkRenderingInfo` dynamic rendering | T2 | Clear color + depth to known values, read back, verify pixels |

### 3.2 First mesh (T3)

| Subtask | Deliverable | Verified by | Done when |
|---------|-------------|-------------|-----------|
| 3.2a ✅ | CMake multi-stage shader support: `starforge_add_shader` accepts `OUTPUT_NAME`, `STAGE`, and `ENTRY` arguments so vertex and fragment stages from the same `.slang` file produce distinct `.spv` files; entry points are unique per stage (`vertexMain`, `fragmentMain`, `computeMain`) | Build + T1 | `starforge_add_shader` supports `OUTPUT_NAME` argument; build succeeds |
| 3.2b | BDA geometry infrastructure: shader-side buffer references for vertex/index data, `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` buffers, BDA-aligned allocation | T2 | BDA buffer created, address queried, and read back headlessly with zero validation errors (test_bda_simple.cpp) |
| 3.2c ✅ | Graphics pipeline helper (dynamic rendering, no render passes, no framebuffer objects): color attachment formats, depth attachment format, sample count, primitive topology, rasterization state, front-face winding, back-face culling, depth test/write settings, shader stages | T3 | Pipeline created and destroyed cleanly; triangle rendered with non-zero pixels |
| 3.2d ✅ | Indexed cube with per-face colors and an asymmetric triangle; orientation verified (winding, Y-flip) | T3 | Cube rendered with per-face colors; non-zero pixels verified; zero validation errors |

### 3.3 Depth and reverse-Z (T1 + T3)

| Subtask | Deliverable | Verified by | Done when |
|---------|-------------|-------------|-----------|
| 3.3a ✅ | Reverse-Z infinite projection math (already in `math.hpp`: `perspectiveVulkanReverseZInfinite`) | T1 | Unit tests for depth ordering at near and far; monotonic depth decrease verified; near -> 1, far -> 0 |
| 3.3b ✅ | Depth buffer integration: clear depth to 0, compare with `GREATER`, viewport depth range 0..1, infinite-far projection. Test both draw orders (farther-first and nearer-first) with overlapping surfaces in screen space | T3 | Two overlapping surfaces render in correct order regardless of draw order; red (near) wins over blue (far) in both orderings; zero validation errors |

### 3.4 Translation-invariance proof (T1 + T3)

| Subtask | Deliverable | Verified by | Done when |
|---------|-------------|-------------|-----------|
| 3.4a ✅ | Minimal render-object submission (mesh + WorldPosition + rotation + scale) using push constants + BDA for vertex data (depends on M2.4) | T1 | RenderObject created; camera-relative model matrix verified at origin and 1e12 m; rotation and scale tested; large-object planet surface verified |
| 3.4b ✅ | Invariance: T1 assertion that float matrices sent to the GPU are bit-identical (0 ULP) at two origins with the same relative arrangement (origin 0 through ~9.4e24 m, object at 100 m from camera). Rotation, multiple distances, and planet-near-surface case all verified. Naive-float control demonstrates 0 m relative distance at 1e12 m (complete precision loss). | T1 + T3 | Float matrices bit-identical across all tested origins; naive float produces 0 m instead of 100 m at 1e12 m |
| 3.4c ✅ | Naive control with error-vs-distance table at 1e3, 1e6, 1e9, and 1e12 m; build the control inside the test by feeding pre-rounded float positions (no naive path in the engine). Record position error in metres and pixel difference in the rendered image. Scene must contain a feature small enough that precision loss is visually measurable | T3 | Error table committed and printed; naive control produces 0 m relative distance at 1e12 m (complete precision loss); zero validation errors |

### 3.5 Sandbox integration (T5)

| Subtask | Deliverable | Verified by | Done when |
|---------|-------------|-------------|-----------|
| 3.5 ✅ | Present cleared frames with depth buffer in the sandbox window; swapchain resize recreates the depth buffer; minimize handling skips rendering | T5 | Window opens, presents cleared frames, resize/minimize checklist passes, clean shutdown with zero validation errors |

Exit: vision goal "accurate positioning across astronomical distances" proven end to end.

Note: M3 delivers single-draw mesh rendering. Basic instanced drawing (vkCmdDrawIndexed with instanceCount > 1) is a small
prerequisite for M4.3b and can be added as a minor M3 extension or as an early M4 task. Multi-draw indirect is introduced in
M4.3b as part of the competent CPU-driven baseline.

## M4 – Measure

### 4.0 Decision notes (Review)

Before implementation, resolve these design decisions and record them as ADRs:
- **Tracy:** Adopt or not. Decide using project profiling needs; do not add
  speculatively. Arch packages Tracy. Wrap in a project macro so it can be
  compiled out entirely. Tracy is a profiling tool; the benchmark infrastructure
  is a measurement system — they complement each other, not depend on each
  other. Benchmark with Tracy disabled unless specifically profiling.
  Tracy's GPU zones use `VK_EXT_calibrated_timestamps`; verify RADV supports
  it on the target driver. Native Vulkan timestamp collection (GPU duration
  queries) is independent of Tracy and does not require calibrated timestamps.
- **Benchmark methodology:** Offscreen (headless, no vsync/compositor interference).
  Two modes: latency (individual workload completion) and throughput (sustained
  work with multiple frames in flight). GPU clocks: record temperature, clock,
  power state, CPU frequency/governor, and whether other GPU-intensive processes
  are running. Support two modes: normal (record only) and controlled (optionally
  pin clocks, documented). Validation off, Release build only. No console output
  during measured section (collect in memory, write JSON after run).
- **Tracy GPU zones:** Use `VK_EXT_debug_utils` labels for RenderDoc/Radeon
  profiler identification (distinct from Tracy's GPU zones).
- **Metrics:** Three independently measured quantities:
  - CPU execution time (command recording, submission, frame preparation — separate)
  - GPU execution time (timestamp-based, per-pass named scopes)
  - Frame interval / throughput (end-to-end frame production rate)
  These are not interchangeable; CPU and GPU work can overlap.
  Also record: draw calls, instances submitted, instances visible, vertices/
  indices processed, command buffers submitted, pipeline changes, VMA memory
  allocated/used, allocation count, block count, heap usage.
- **JSON schema:** versioned, with commit hash + dirty flag + diff hash for
  experimental builds, GPU/driver/kernel/CPU model, compiler/build flags,
  resolution, present mode (null for headless), scene parameters + seed,
  `STARFORGE_DISABLE_FEATURES` setting. Raw per-frame samples retained alongside
  summary statistics. Explicit null values for unavailable measurements.
- **Noise policy:** Measure noise floor across several separate process launches
  (not within one run). Each process: warm-up frames + fixed measured workload.
  Compare variation across runs. Set acceptance threshold from measured data,
  not a guess. Future: confidence intervals or bootstrap resampling.
- **Budgets:** Set top-down from a frame-time target (e.g., 144 fps = 6.94 ms
  on the 7900 XTX), split across subsystems (CPU preparation, CPU submission,
  GPU rendering, sync/stall, headroom). The baseline shows the gap to close, not
  the target. State explicitly: single-GPU numbers don't transfer to other
  hardware. 144 fps is a development-machine target, not a universal requirement.
- **Workload profiles:** At least two: submission-stress (simple geometry, many
  objects, low screen coverage, high draw-call pressure) and geometry/raster-stress
  (complex meshes, higher screen coverage, more triangles, more fragment work).
- **Thermal/environmental state:** Record GPU temperature, clock, power state or
  consumption where available. Record CPU frequency/governor. Note whether other
  GPU-intensive processes are running.
- **Memory reporting:** VMA memory snapshots (allocated/used bytes, allocation
  count, block count, heap usage/budget) captured before and after benchmark,
  not during timed rendering.

### 4.1 Timestamp and statistics infrastructure

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 4.1a ✅ | Statistics library: percentile calculation (median, p95, p99, min, max, stddev). Define percentile algorithm (interpolated). Handle empty, single, two, identical, outlier, large, and non-finite samples | T1 | Pure CPU unit tests pass; percentiles verified against known distributions |
| 4.1b ✅ | JSON writer with versioned schema (commit, GPU, driver, build flags, scene params, metrics, raw samples). Schema file committed. Explicit units and null values | T1 | Serialization round-trips; schema validated |
| 4.1c ✅ | GPU timestamps: per-frame query pools (one per frame in flight), `timestampValidBits`/`timestampPeriod` handling, wraparound support, `vkCmdWriteTimestamp2` with `synchronization2`, named scopes emitting `VK_EXT_debug_utils` labels. Async result retrieval (no per-frame GPU stall) | T2 | Timestamps available on selected queue; conversion correct; wraparound handled; repeated workloads produce stable non-zero durations; larger workloads produce correspondingly larger execution times; unsupported queues reported clearly |
| 4.1d ✅ | CPU frame timing: wall-clock per frame, command recording duration, submission duration, frame preparation duration, frame interval. No sleeps during measured section | T1/T2 | CPU time recorded separately from GPU time; CpuTimer + FrameTimingCollector implemented and tested |
| 4.1e ✅ | VMA memory statistics: allocated/used bytes, allocation count, block count, heap usage/budget. Captured before/after benchmark, not during timed rendering | T1/T2 | Memory snapshots reported without affecting timed measurements; `Allocator::captureMemorySnapshot()` implemented and tested |
| 4.1f ✅ | Timestamp validation and availability: zero timestamp bits handled, wraparound supported, timestamp conversion verified, unsupported queues reported clearly | T2 | All timestamp edge cases covered by tests (unsupported queue reports zero valid bits) |

Dependencies: 4.1a and 4.1b can start immediately (pure CPU). 4.1c needs a
compute workload (available from M1.6).

### 4.2 Benchmark harness (T4)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 4.2a | Headless harness: fixed frame count, seeded deterministic scene, system metadata collection, refuses non-Release/validation builds. No SDL window or swapchain | T4 | Harness runs N frames offscreen with no vsync/compositor interference |
| 4.2b | Warm-up and benchmark phases: warm-up samples excluded; measured sample count fixed and recorded | T4 | Warm-up excluded; measured count recorded |
| 4.2c | Noise-floor measurement across separate process launches; set acceptance threshold from data | T4 | Threshold documented and derived from measured noise |
| 4.2d | Latency and throughput modes: latency measures individual workload completion; throughput measures sustained work with multiple frames in flight. Both produce independent results and metadata | T4 | Both modes produce independent results |
| 4.2e | Benchmark environment reporting: GPU/CPU environmental data recorded where available; unavailable values explicitly reported as null | T4 | Environmental data recorded or explicitly null |
| 4.2f | Compare tool: takes two JSON baselines, reports per-metric ratios, flags differences exceeding noise threshold. Refuses incompatible workloads; warns on GPU/driver/compiler differences; identifies missing metrics; returns non-zero exit code on regression | T1 | Tool correctly identifies regressions above noise floor; incompatible configs rejected |

Dependencies: needs 4.1a–4.1f.

### 4.3 CPU-driven baseline (T4)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 4.3a | Scene spec: geometry complexity, mesh count, instance count, material count, triangle count, object size distribution, spatial distribution, camera path, visible fraction, screen coverage, overdraw, resolution, seed. Submission-stress and geometry/raster-stress profiles. (Documented, reviewed) | Review | Spec agreed and committed |
| 4.3b | Naive CPU-driven baseline (no culling, one draw per object) and competent CPU-driven baseline (CPU frustum culling + instanced draws + multi-draw-indirect, recorded separately). Identical geometry, shaders, materials, camera, visible objects, resolution, depth config | T4 | Both baselines recorded; competent baseline is the comparison target for M5 |
| 4.3c | Sweep: 10k, 100k, 1M instances × ~100%, ~10%, ~1% visible fraction. Naive: 10k and 100k separate draws. Competent: 10k, 100k, 1M. Optional 1M naive stress test (clearly labelled) | T4 | Baseline curves recorded (including 1M even if CPU path is slow) |
| 4.3d | Per-machine baseline files (`benchmarks/baselines/<gpu>-<driver>/`) with full metadata; directory organised by GPU family and driver, JSON contains full hardware identity. Re-baselining procedure documented | T4 | Baseline files committed; compare tool works across files |

Dependencies: needs M3 (instanced draws, depth, camera). Note: M3 currently
delivers triangle/cube rendering but not instanced draws. Add basic instanced
drawing to M3 or as a small M4 prerequisite (4.3a can start during M3).
4.3a (scene spec) can start during M3.

### 4.4 Performance budgets (Review + T4)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 4.4a | Frame-time target and budget document: target frame interval, CPU preparation budget, CPU submission budget, GPU rendering budget, sync/stall budget, headroom. Committed as a document | Review | Budget document committed with explicit subsystem allocations |
| 4.4b | Baseline-to-budget comparison: measured baseline compared against target budgets, gaps documented | T4 | Baseline vs. budget comparison committed |
| 4.4c | Hardware scope: RX 7900 XTX development target clearly distinguished from future hardware-independent goals | Review | Hardware scope documented |

### Exit

CPU, GPU, latency, and throughput measurements are independently available.
Benchmark runs are reproducible and contain complete workload/system metadata.
Measurement noise has been characterized across independent process launches.
Naive and competent CPU-driven baselines are committed. Performance budgets
are documented. VMA memory usage is reported separately from timed rendering
measurements. The baseline comparison tool detects regressions beyond the
established noise threshold.

## M5 – GPU-Driven Rendering (goal and proof)

- **Goal:** bindless resources and buffer device address; GPU culling
  produces indirect draws; scale to 1M instances.
- **Proof:** compute culling output equals the CPU reference (T2);
  1M instances beat the M4 baseline (T4); runs correctly with
  `STARFORGE_DISABLE_FEATURES=drawIndirectCount,bindless` (fallbacks).

## M6 – Deterministic Exploration (goal and proof)

- **Goal:** content is a pure function of position:
  `generate(seed, cell, scale level)`. No zones, no loading screens;
  content near the camera is generated on demand. Only player changes
  are stored, keyed by cell and object ID. Scale levels use the
  power-of-two cell hierarchy decided in M2.
- **Proof:** the same cell generates bit-identical content in any visit
  order and on any machine (T1); a simulated long flight reaches
  generated content with positions still precise (T1 + T3);
  translation-invariance holds at the maximum extent. Distant,
  not-yet-resolvable content (fields, stars) has a cheap representation
  so there is something to discover from far away.

## M7 – Planets

- **Goal:** LOD'd planet surface, then atmosphere.
- **Proof:** CPU quadtree LOD selection (T1); continuous descent from
  orbit to surface with bounded patch count and no cracks (T3, T4).

## M8 – Environment and Effects

- **Goal:** stars, nebulae, dust; particles, weapons, shields.
- **Proof:** golden images and frame-time budgets at target scene
  sizes. Mesh shaders only behind `GpuCapabilities.meshShader` with a
  non-mesh fallback.

## M9 – Public API

- **Goal:** developer-facing API shaped from the working slice.
- **Proof:** the sandbox scene rebuilt using only `include/starforge/`,
  with no Vulkan types visible.
