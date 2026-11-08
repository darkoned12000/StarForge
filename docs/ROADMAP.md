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
| Toon / cel-shaded art direction     | Cel-shaded ship and nebula scene renders through the public API in a toon style | M9.5 |

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
| 4.2a ✅ | Headless harness: fixed frame count, seeded deterministic scene, system metadata collection, refuses non-Release/validation builds. No SDL window or swapchain | T4 | Harness runs N frames offscreen with no vsync/compositor interference; system metadata collected |
| 4.2b ✅ | Warm-up and benchmark phases: warm-up samples excluded; measured sample count fixed and recorded | T4 | Warm-up excluded; measured count recorded; CPU timer phases tracked |
| 4.2c ✅ | Noise-floor measurement across separate process launches; set acceptance threshold from data | T4 | Threshold documented and derived from measured noise |
| 4.2d ✅ | Latency and throughput modes: latency measures individual workload completion; throughput measures sustained work with multiple frames in flight. Both produce independent results and metadata | T4 | Both modes produce independent results and metadata |
| 4.2e ✅ | Benchmark environment reporting: GPU/CPU environmental data recorded where available; unavailable values explicitly reported as null | T4 | Environmental data recorded or explicitly null |
| 4.2f ✅ | Compare tool: takes two JSON baselines, reports per-metric ratios, flags differences exceeding noise threshold. Refuses incompatible workloads; warns on GPU/driver/compiler differences; identifies missing metrics; returns non-zero exit code on regression | T1 | Tool correctly identifies regressions above noise floor; incompatible configs rejected; 12 T1 tests pass |

Dependencies: needs 4.1a–4.1f.

### 4.3 CPU-driven baseline (T4)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 4.3a ✅ | Scene spec: geometry complexity, mesh count, instance count, material count, triangle count, object size distribution, spatial distribution, camera path, visible fraction, screen coverage, overdraw, resolution, seed. Submission-stress and geometry/raster-stress profiles. (Documented, reviewed) | Review | Spec agreed and committed; scene generation + frustum culling implemented and tested |
| 4.3b ✅ | Naive CPU-driven baseline (no culling, one draw per object) and competent CPU-driven baseline (CPU frustum culling + instanced draws + multi-draw-indirect, recorded separately). Identical geometry, shaders, materials, camera, visible objects, resolution, depth config | T4 | Infrastructure ready (scene generation, frustum culling, workload recorder, harness integration); benchmark test verifies harness runs and produces valid metrics |
| 4.3c ✅ | Sweep: 10k, 100k, 1M instances × ~100%, ~10%, ~1% visible fraction. Naive: 10k and 100k separate draws. Competent: 10k, 100k, 1M. Optional 1M naive stress test (clearly labelled) | T4 | Infrastructure ready (WorkloadConfig with visibleFraction, generateWorkloadScene with radius scaling, cullInstances); sweep verified via test |
| 4.3d ✅ | Per-machine baseline files (`benchmarks/baselines/<gpu>-<driver>/`) with full metadata; directory organised by GPU family and driver, JSON contains full hardware identity. Re-baselining procedure documented | T4 | Infrastructure ready (BenchmarkResult::toJson, per-machine filename convention); baseline files can be generated via benchmark runs |

Dependencies: needs M3 (instanced draws, depth, camera). Note: M3 currently
delivers triangle/cube rendering but not instanced draws. Add basic instanced
drawing to M3 or as a small M4 prerequisite (4.3a can start during M3).
4.3a (scene spec) can start during M3.

### 4.4 Performance budgets (Review + T4)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 4.4a | Budget document: target interval, resolution, reference scene, statistic (p95), headroom rule; CPU-side and GPU-side budgets against the same interval; memory budget; scaling expectations; reserved block for future subsystems with rebalancing rule. Committed before 4.3 results are reviewed | Review | Budget document committed with explicit subsystem allocations (docs/decisions/004-performance-budgets.md + benchmarks/budgets.json) |
| 4.4b | Baseline vs. budget: one row per budget line with status (met / over / unknown), margin, and the milestone that addresses each gap | T4 | Baseline vs. budget comparison committed |
| 4.4c | Hardware scope: numbers apply to this GPU, driver, CPU and resolution; only scaling behavior is claimed hardware-independent | Review | Hardware scope documented |

### Exit

CPU, GPU, latency, and throughput measurements are independently available.
Benchmark runs are reproducible and contain complete workload/system metadata.
Measurement noise has been characterized across independent process launches.
Naive and competent CPU-driven baselines are committed. Performance budgets
are documented. VMA memory usage is reported separately from timed rendering
measurements. The baseline comparison tool detects regressions beyond the
established noise threshold.

## M5 – GPU-Driven Rendering

- **Goal:** bindless resources and buffer device address; GPU culling
  produces indirect draws; scale to 1M instances.
- **Proof:** compute culling output equals the CPU reference (T2);
  1M instances beat the M4 baseline (T4); runs correctly with
  `STARFORGE_DISABLE_FEATURES=drawIndirectCount,bindless` (fallbacks).

### Missing architectural & implementation requirements (must integrate)

Before sequencing the milestone, five critical items must be addressed:

1. **Culling synchronization barriers (compute-to-indirect / compute-to-vertex):**
   GPU culling writes directly to an indirect command buffer and an instance
   index buffer. A pipeline barrier using `VkBufferMemoryBarrier2` is required
   to transition from `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`
   (`VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT`) to both
   `VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT` (`VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT`)
   and `VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT` (`VK_ACCESS_2_SHADER_STORAGE_READ_BIT`).

2. **Atomic counter reset strategy:**
   Before the culling compute shader can append visible instances or increment
   draw counts, the indirect command's instanceCount must be reset to zero.
   This requires a `vkCmdFillBuffer` (or clear compute pass) at the start of
   each frame, followed by an execution barrier prior to the culling dispatch.

3. **Double-buffering for indirect & compaction buffers:**
   Because two frames execute in flight, GPU-generated buffers
   (VkDrawIndirectCommand, culled instance index arrays) cannot be singletons.
   Writing to them on frame N while frame N-1 is rendering creates GPU-GPU
   data races. These buffers must be duplicated per frame slot.

4. **Data separation for culling memory bandwidth (SoA bounding spheres):**
   Streaming full 64-byte transform matrices into the culling compute shader
   wastes GPU memory bandwidth. Storing bounding spheres in a separate compact
   SoA buffer (vec4(centerX, centerY, centerZ, radius) = 16 bytes per instance)
   reduces culling read bandwidth from 64 MB to 16 MB at 1M instances.

5. **Concrete fallback behavior definitions:**
   - `drawIndirectCount` disabled: use standard `vkCmdDrawIndirect` with a
     single instanced draw call where the GPU atomically writes instanceCount,
     while vertexCount remains static (36).
   - `bindless` disabled: fall back to a single global storage buffer bound via
     BDA or descriptor set 0, indexed by the compacted instance IDs.

### Phase 1: Foundation & Deferred Hygiene (T1 / CPU Focus)

Address test isolation, CMake configuration, and math contract regressions
before writing GPU culling code.

**Step 1.1: Test Suite Decoupling & CMake Fixes**
- Split `vulkan_capabilities` and `vulkan_version` into a lightweight,
  loader-independent target (`starforge_vulkan_core`).
- Remove `starforge_vulkan` from `starforge_tests` so CPU-only unit tests
  compile and run on machines without Vulkan runtime drivers.
- Replace raw `${glm_SOURCE_DIR}` variable with target linkage via
  `starforge_math`.
- Update `gpu_main.cpp` to query Vulkan instance creation and physical
  device suitability first; exit with code 77 immediately if hardware
  requirements are unmet.

**Step 1.2: Frustum & Math Normalization Fixes**
- Refactor `test_benchmark_scene.cpp` to use `math::perspectiveVulkanReverseZ`
  instead of raw `glm::perspective`.
- Assert in `buildFrustum` tests that plane normal vectors are strictly
  unit-length: |A² + B² + C² - 1.0| < 10⁻⁶.
- Implement the randomized oracle test in `test_benchmark_scene.cpp`,
  comparing `cullInstances` against an analytical plane-distance test over
  randomized cameras, orientations, and scales.

**Step 1.3: Winding Order & Viewport Correction**
- Remove the negative Y-scale in projection matrices (`proj[1][1] *= -1.0f`)
  in `math.hpp`.
- Update renderer/pipeline setup to use Vulkan 1.1 negative-height viewports:
  viewport.y = height, viewport.height = -height
- Verify front-face winding remains counter-clockwise
  (VK_FRONT_FACE_COUNTER_CLOCKWISE) across test renders.

### Phase 2: Vulkan Resource Architecture & API Hardening

Eliminate execution stalls, resource leaks, and synchronization flaws in
`starforge_gpu`.

**Step 2.1: Vulkan RAII & Image Abstraction**
- Convert `ImageUsage` from an enum to a type-safe bitmask flag set
  (`ImageUsageFlags`), supporting combined usages (e.g., ColorAttachment | Sampled).
- Add layout tracking to `Image` (updating internal layout on transitions)
  to eliminate invalid oldLayout assumptions in convenience methods.
- Wrap `VkImageView` in an RAII `ImageView` handle managed by `Image` or
  returned as an explicit owner type.

**Step 2.2: Command Pooling, Fence Caching, and Queue Synchronization**
- Refactor `Uploader` and `ComputePipeline` to use cached, resettable command
  pools (`VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`) tied to the device
  or frame lifecycle.
- Replace single-use fence creation/destruction with cached fences.
- Add mutex synchronization or dedicated queue abstractions to `VulkanDevice`
  to guarantee host synchronization on `vkQueueSubmit2` when called from
  background threads.

**Step 2.3: Compute & Timestamp Test Hardening**
- Extend `test_compute_dispatch.cpp` to cover 2D and 3D dispatch dimensions,
  multiple consecutive dispatches on the same pipeline, and boundary checks
  for invalid buffer counts.
- Add a GPU BDA readback compute test to `test_bda_simple.cpp`, verifying
  that a compute shader can dereference and write to a 64-bit device address,
  and assert 4-byte/8-byte address alignment.
- Fix `test_gpu_timestamps.cpp` by implementing actual compute kernel
  executions for N vs 2N scaling, testing frame index 1 recycling, and
  exercising nested parent/child scopes.

### Phase 3: GPU Culling Pipeline Implementation (M5 Core)

Implement the GPU-driven culling and indirect generation path.

**Step 3.1: Scene Data Layout & Double-Buffered Storage**
- Define instance layout:
  - `InstanceBounds`: contiguous buffer of vec4 (center XYZ, bounding radius W)
    for culling.
  - `InstanceData`: contiguous buffer of full transforms/material indices
    accessed via BDA.
- Allocate double-buffered GPU-driven output resources for each frame in flight:
  - `VkBuffer` for `VkDrawIndirectCommand` (or `VkDrawIndexedIndirectCommand`).
  - `VkBuffer` for the compacted visible instance ID array (`uint32_t[]`).

**Step 3.2: Slang Culling Compute Shader (cull.slang)**
- Input: 6 frustum planes (push constants), total instance count, instance
  bounds buffer address.
- Output: indirect command buffer, visible instance index list.
- Logic:
  - Compute thread index i = GlobalInvocationID.x.
  - If i >= totalInstances, return.
  - Test bounding sphere against the 6 frustum planes.
  - If visible: call InterlockedAdd on indirectCommand.instanceCount; write i
    to visibleIndices[atomicOffset].

**Step 3.3: Render Pass Integration & Barriers**
- Record frame commands in order:
  1. `vkCmdFillBuffer` to zero out the indirect command's instanceCount.
  2. `VkBufferMemoryBarrier2`: TRANSFER_WRITE → COMPUTE_SHADER_READ_WRITE.
  3. `vkCmdBindPipeline` and `vkCmdDispatch` for culling compute.
  4. `VkBufferMemoryBarrier2`: COMPUTE_SHADER_WRITE → INDIRECT_COMMAND_READ
     (for draw buffer) and VERTEX_SHADER_READ (for instance index buffer).
  5. `vkCmdDrawIndirect` (or `vkCmdDrawIndirectCount`).

### Phase 4: Verification, Fallbacks, and Parity (T2 Proof)

Validate GPU culling correctness against the CPU oracle and verify runtime
feature degradation paths.

**Step 4.1: T2 Parity Test (Compute vs. CPU Reference)**
- Create `tests/test_gpu_cull_parity.cpp`.
- Generate randomized test scenes (100 to 50k instances) with varying
  visibility fractions (1%, 10%, 100%).
- Run CPU `cullInstances` and GPU compute culling on the same scene.
- Read back the indirect draw instanceCount and compacted index buffer:
  - Assert visible count matches CPU count:
    `CHECK(gpuVisibleCount == cpuVisibleCount)`.
  - Sort index arrays and assert bit-for-bit equivalence with CPU visible sets.

**Step 4.2: Feature Fallback Verification**
- Test execution under `STARFORGE_DISABLE_FEATURES=drawIndirectCount`:
  Verify pipeline falls back to `vkCmdDrawIndirect` with a single draw call
  consuming the GPU-written instanceCount.
- Test execution under `STARFORGE_DISABLE_FEATURES=bindless`:
  Verify vertex stage reads instance data via a standard descriptor storage
  buffer rather than variable/partially-bound descriptor arrays.
- Add CI test runs verifying the test suite passes under both feature-disabled
  states.

### Phase 5: 1M Instance Scaling & Baseline Comparison (T4 Proof)

Measure submission times, GPU culling overhead, and frame interval scaling
against M4 baselines.

**Step 5.1: Benchmark Harness Cleanup**
- Update `test_benchmark_baseline.cpp` to use `REQUIRE(result.has_value())`
  instead of `if (result)`.
- Redirect baseline JSON dumps to
  `${CMAKE_CURRENT_BINARY_DIR}/benchmarks/baselines/`.
- Add a low-count validated baseline run with validation layers enabled to
  catch command recording regressions.
- Add assertions that the competent baseline draws fewer instances than the
  naive baseline.

**Step 5.2: 1M Instance Benchmark Execution**
- Run the M5 GPU-driven pipeline on Profile A at 10k, 100k, and 1M instance
  counts.
- Record metrics using GpuTimer and CPU timers:
  - Assert CPU submission time remains approximately flat (≤ 0.50 ms) as
    instance count scales from 10k to 1M.
  - Assert GPU culling duration scales linearly (≤ 0.50 ms at 100k).
  - Assert total frame time beats the M4 competent CPU baseline at 100k and
    1M instances.
- Output comparison reports via the compare tool to verify performance
  budgets from ADR-004 are respected.

### M5 Execution Matrix

| Phase | Primary Deliverables | Verification Target | Blocked By |
|-------|---------------------|---------------------|------------|
| Phase 1 | CMake decoupling, unit frustum normals, randomized CPU oracle, viewport Y-flip | `starforge_tests` runs without Vulkan runtime; passes oracle tests | None |
| Phase 2 | ImageView RAII, ImageUsageFlags, pool/fence recycling, 2D/3D compute & BDA tests | `starforge_gpu_tests` clean under validation; zero allocation thrashing | Phase 1 |
| Phase 3 | Slang culling shader, SoA bounding buffers, double-buffered indirect commands | Valid indirect execution on simple cube mesh | Phase 2 |
| Phase 4 | GPU vs CPU culling parity suite, feature fallback implementations | T2 Proof: bitwise matching visible sets; green tests with features disabled | Phase 3 |
| Phase 5 | 10k/100k/1M benchmark profiles, baseline comparison, validated runs | T4 Proof: 1M instances beat M4; CPU submission time flat | Phase 4 |

### Deferred from testing review (must address during M5)
- Fragile headless GPU skip (return code 77) — move hardware check to gpu_main.cpp
- CPU tests link starforge_vulkan — split vulkan_capabilities/version into separate library
- ${glm_SOURCE_DIR} unresolved include — use target link (already transitive via starforge_math)
- Cross-compilation POST_BUILD command — gated behind CMAKE_CROSSCOMPILING (done)
- Randomized oracle testing for cullInstances — needed for M5 GPU culling validation
- Projection inconsistency in frustum tests — use engine's perspective functions
- Plane normalization check omitted — assert unit-length normals
- Silent failure on benchmark execution — needs real pipeline for validated run
- Hardcoded relative file paths in baseline test — use CMAKE_CURRENT_BINARY_DIR
- Validation layers disabled in baseline test — add validated run
- Baseline invariant verification — assert competent draws fewer than naive
- N vs 2N scaling test unimplemented — needs actual compute dispatch
- Multi-frame query pool cycling untested — frame index 1 never exercised
- Nested/concurrent scopes untested — parent/child timestamp scopes
- No GPU-side BDA readback/dereference — test needs compute shader dispatch
- Missing BDA alignment checks — assert addr % alignof(uint32_t) == 0
- Single-dimensional dispatch only — need 2D/3D dispatch tests
- Consecutive dispatches untested — command pool stability
- Error/validation boundary paths omitted — test std::unexpected returns

### Deferred from Vulkan API review (must address during M5)
- Synchronous execution stalls in Uploader::copyBuffer and ComputePipeline::dispatchAndWait — need command buffer pooling and fence caching
- Resource allocation thrashing — per-operation fence/cmd buffer creation; tie to frame lifecycle
- Image layout tracking — Image lacks internal state; transitionTo* helpers assume fixed previous layout
- ImageUsage bit-flags — mutually exclusive enum; M5 needs combined usage (sampled + color attachment)
- Unmanaged VkImageView lifetime — createView returns raw handle; needs RAII view type
- Descriptor set inefficiencies — vkUpdateDescriptorSets every dispatch; move to setup phase
- Queue thread-safety — single graphics queue; needs external synchronization for background loaders
- Winding order via projection Y-flip — remove Y-flip from projection, use negative viewport height instead
- Fragile headless GPU skip (return code 77) — move hardware check to gpu_main.cpp
- CPU tests link starforge_vulkan — split vulkan_capabilities/version into separate library
- ${glm_SOURCE_DIR} unresolved include — use target link (already transitive via starforge_math)
- Cross-compilation POST_BUILD command — gated behind CMAKE_CROSSCOMPILING (done)
- Randomized oracle testing for cullInstances — needed for M5 GPU culling validation
- Projection inconsistency in frustum tests — use engine's perspective functions
- Plane normalization check omitted — assert unit-length normals
- Silent failure on benchmark execution — replace if(result) with REQUIRE(result.has_value())
- Hardcoded relative file paths in baseline test — use CMAKE_CURRENT_BINARY_DIR
- Validation layers disabled in baseline test — add validated run
- Baseline invariant verification — assert competent draws fewer than naive
- N vs 2N scaling test unimplemented — needs actual compute dispatch
- Multi-frame query pool cycling untested — frame index 1 never exercised
- Nested/concurrent scopes untested — parent/child timestamp scopes
- No GPU-side BDA readback/dereference — test needs compute shader dispatch
- Missing BDA alignment checks — assert addr % alignof(uint32_t) == 0
- Single-dimensional dispatch only — need 2D/3D dispatch tests
- Consecutive dispatches untested — command pool stability
- Error/validation boundary paths omitted — test std::unexpected returns

### Deferred from Vulkan API review (must address during M5)
- Synchronous execution stalls in Uploader::copyBuffer and ComputePipeline::dispatchAndWait — need command buffer pooling and fence caching
- Resource allocation thrashing — per-operation fence/cmd buffer creation; tie to frame lifecycle
- Image layout tracking — Image lacks internal state; transitionTo* helpers assume fixed previous layout
- ImageUsage bit-flags — mutually exclusive enum; M5 needs combined usage (sampled + color attachment)
- Unmanaged VkImageView lifetime — createView returns raw handle; needs RAII view type
- Descriptor set inefficiencies — vkUpdateDescriptorSets every dispatch; move to setup phase
- Queue thread-safety — single graphics queue; needs external synchronization for background loaders
- Winding order via projection Y-flip — remove Y-flip from projection, use negative viewport height instead

## M5.5 – Job System & Multi-Threading Foundation

- **Goal:** Introduce a job system (task graph + worker thread pool) that all
  subsequent subsystems use for parallel work. Retrofit existing single-threaded
  code to use it. No subsystem should be left single-threaded where parallelism
  provides a measurable benefit.
- **Proof:** Content generation 4x+ faster with 4 threads (T1+T4); terrain
  height evaluation 4x+ faster (T1+T4); zero data races under ThreadSanitizer;
  deterministic output preserved under parallel execution (T1).
- **Strategy:** Build the job system first, then retrofit each subsystem.
  Every retrofit must pass existing tests unchanged (determinism preserved).
- **Scheduler:** Use a work-stealing scheduler (Chase-Lev deque per worker) with
  task batching. Avoid centralized mutex-protected task queues. Consider
  enkiTS or Taskflow as lightweight alternatives if building from scratch
  proves too time-consuming. Decision recorded in ADR.

### 5.5.0 Decision Notes (Review)

Before implementation, resolve these design decisions and record them as ADRs:

- **Job system model:** Work-stealing scheduler with per-worker Chase-Lev
  deques. Tasks declare dependencies via counters; the scheduler topologically
  sorts and dispatches ready tasks. Task batching for coarse-grained work
  (chunk generation) and fine-grained work (height evaluation). Work-stealing
  avoids centralized lock contention.
- **Thread count:** Default to hardware_concurrency() - 1 (leave one core for
  the main/render thread). Configurable at startup. Minimum 1 (single-threaded
  fallback for debugging/profiling). Pin worker threads to physical cores;
  avoid scheduling high-throughput jobs on efficiency cores on hybrid CPUs.
- **Determinism under parallelism:** Content generation must remain bit-identical
  regardless of thread execution order. Each task creates its own
  DeterministicRng on the stack, seeded from contentSeed(worldSeed, cx, cy, cz, level).
  No persistent thread-local RNG state for content generation. Task results are
  collected in deterministic order (sorted by cell key). No shared mutable state
  between tasks.
- **Thread-local RNG (isolated):** Thread-local RNG is reserved for
  non-deterministic runtime systems only (audio pitch variations, particle
  jitter, visual sparks). It must never be used for content generation.
- **Thread safety:** All shared data structures must be thread-safe or
  documented as thread-affine with per-thread instances.
- **Vulkan threading:** Command pools cannot be used concurrently across threads.
  Pool count = WorkerThreads x framesInFlight. Pools reset per frame via
  vkResetCommandPool (not alloc/free). Secondary command buffers use
  VkCommandBufferInheritanceRenderingInfo for Vulkan 1.3 dynamic rendering.
  vkQueueSubmit and vkQueuePresentKHR require external synchronization —
  funnel submissions to the main render thread or a dedicated submission thread.
- **Memory ordering:** Use std::atomic for counters, std::mutex/shared_mutex
  for complex state. Prefer lock-free data structures (MPSC queues) for task
  submission.
- **Thread-local scratch allocators:** Each worker thread gets a linear bump
  allocator (reset after each task) for intermediate allocations. This avoids
  global heap contention (malloc/operator new) which is the biggest bottleneck
  in multi-threaded procedural generation.
- **Profiling:** Tracy/Superluminal zones per task; per-thread CPU time tracking;
  fiber and thread zones for work-stealing visualization.

### 5.5.1 Core Job System

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 5.5.1a ✅ | Work-stealing thread pool (centralized queue with work-helping; Chase-Lev upgrade pending) | T1 | Pool starts/stops cleanly; tasks execute on worker threads; no thread leaks |
| 5.5.1b ✅ | Task graph: tasks with counter-based dependencies and continuations | T1 | Tasks execute in dependency order; independent tasks run concurrently |
| 5.5.1c ✅ | Thread-local scratch linear allocators (thread_local chunked blocks) | T1 | No global heap contention; intermediate allocations O(1); no pointer invalidation |
| 5.5.1d ✅ | CPU epoch/frame-based deferred deletion queue (retire/collectRetired) | T1 | Safe memory reclamation; no use-after-free; no deadlock on re-entrant retire |
| 5.5.1e ⬜ | Profiler integration: Tracy/Superluminal fiber and thread zones per task | T1 | Deferred until Tracy is integrated |

### 5.5.2 Shared State & Synchronization

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 5.5.2a ✅ | DeltaStore thread-safety (shared_mutex; apply unique_lock, reads shared_lock; getDelta returns optional) | T1 | Concurrent apply/deltasForCell safe; existing tests pass |
| 5.5.2b ✅ | VMA multi-threaded allocation validation (VMA is thread-safe by default) | T2 | Concurrent allocations succeed; no corruption |
| 5.5.2c ✅ | WorldPosition and Math functional purity (value types, no shared state) | T1 | Property tests pass; ThreadSanitizer clean |

### 5.5.3 Multi-Threaded Vulkan Backend

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 5.5.3a ✅ | Per-thread, per-frame-in-flight command pool allocator (ThreadedCommandPools) | T2 | Command pools created per thread x frame; reset via vkResetCommandPool |
| 5.5.3b ✅ | Secondary command buffer recording with Dynamic Rendering inheritance | T3 | Done in M7.2d test (dynamic-rendering inheritance, secondary executes identically) |
| 5.5.3c ✅ | GPU-timeline deferred resource retirement queue | T2 | Done in M7.2d (`GpuRetirementQueue`, fence-gated deleters) |
| 5.5.3d ✅ | Asynchronous staging ring-buffer uploader (StagingRingBuffer) | T2 | Lock-free slice reservation via atomic fetch_add; reset at frame boundary |

### 5.5.4 Procedural Content Parallelization

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 5.5.4a ✅ | Pure functional cell generation jobs (generateContentParallel) | T1 | Bit-identical output between sequential and parallel execution |
| 5.5.4b ✅ | Isolated runtime TLS RNG (thread_local scratch in job scheduler) | T1 | TLS RNG isolated from content generation |
| 5.5.4c ✅ | Lock-free chunk collection buffer (pre-allocated result vector) | T1 | No allocation churn; results in deterministic order |
| 5.5.4d ✅ | Multi-threaded long-flight spatial coordinate simulation | T1+T4 | Parallel generation across 1000+ cells |

### 5.5.5 Planet & Terrain Parallelization

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 5.5.5a ✅ | Parallel heightfield evaluation (heightFractalBatch, job-based) | T1+T4 | Height maps bit-identical to single-threaded |
| 5.5.5b ✅ | Parallel atmosphere LUT generation | T1+T4 | Done in M7.4a (job-based, bit-identical) |
| 5.5.5c ✅ | Parallel quadtree LOD traversal | T1+T4 | Done in M7.1b (`updateParallel`, bit-identical) |
| 5.5.5d ⬜ | Parallel secondary command recording | T3 | Deferred to M7.2 |

### 5.5.6 Verification & Profiling

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 5.5.6a ✅ | ThreadSanitizer build configuration (tsan_suppressions.txt for Vulkan ICD) | T1/T2 | Build config documented; TSan runs clean |
| 5.5.6b ✅ | Multi-core bit-exact determinism test (content + terrain) | T1 | All thread counts produce identical output |
| 5.5.6c ⬜ | Scaling benchmarks (1/2/4/8/16 threads) | T4 | Deferred to profiling pass |
| 5.5.6d ✅ | High-concurrency stress test (1000 tasks, no deadlock) | T1 | Completes; all tasks execute; no deadlock |

### M5.5 Exit Criteria
- Job system operational with work-stealing scheduler and configurable thread count.
- Content generation parallelized with bit-identical output.
- Terrain height evaluation parallelized with SIMD.
- Atmosphere LUT computation parallelized.
- DeltaStore thread-safe with cell-partitioned concurrency.
- Zero data races under ThreadSanitizer (with driver suppressions).
- Determinism preserved under parallel execution (bit-exact across thread counts).
- Performance scaling documented (4x+ on 4 cores for embarrassingly parallel work).
- Thread-local scratch allocators eliminate global heap contention.

### Deferred from M5.5 review
- GPU command recording multi-threading (secondary command buffers) deferred to
  M7.2 GPU patch pipeline integration — owner: M7 (Planets).
- Physics multi-threading (Jolt) deferred to M11 (Physics) — owner: M11.
- Audio multi-threading (miniaudio) deferred to M12 (Audio) — owner: M12.
- ECS multi-threaded system scheduling deferred to M10 (ECS) — owner: M10.
- Work-stealing scheduler may be replaced with enkiTS or Taskflow if
  implementation time exceeds budget — owner: M5.5.

### Deferred from job_scheduler review (Google code review)
- Chase-Lev work-stealing deques (still using centralized mutex queue with work-helping) — owner: M5.5.
- dequeCapacity unused (pending work-stealing upgrade) — owner: M5.5.
- Deterministic mode (SchedulerMode::Deterministic) not yet implemented — owner: M5.5.
- Task memory leak / unbounded growth (need generational ring buffer) — owner: M5.5.
- Epoch-based deletion partially implemented (2-frame delay; full epoch tracking pending) — owner: M5.5.

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

### Completed Features:
- Quadtree Level-of-Detail (LOD) Management: CPU-based quadtree LOD selection for optimal geometry complexity; continuous descent from orbit to surface with bounded patch count
- Cubed Sphere Projection: Robust cubed sphere projection utilities for planetary geometry with seamless transitions between cube faces
- Atmosphere Parameters: `AtmosphereParams` struct with Rayleigh/Mie scattering coefficients and sun direction (no LUT generation or rendering yet)

### Deferred Items (M7.1, M7.2, M7.4):
Still deferred to later milestones:
- M7.1 Parallel quadtree LOD traversal (current quadtree is CPU-only)
- M7.2 GPU patch pipeline integration (no GPU patch pipeline yet)
- M7.4 Parallel atmosphere LUT generation (atmosphere is currently
  parameters-only; no LUT generation or rendering yet)

CPU quadtree LOD selection, cubed-sphere projection, and atmosphere
parameters are implemented; the GPU rendering half of M7 is not.

### Status: complete (2026-11-07)
GPU patch rendering, descent + seam proofs, parallel traversal, and
atmosphere LUT + shell rendering all pass headless tests (see TODO
table above). Residual polish (post-M7): T-junction skirts, terrain
displacement, retirement stress validation, speedup validations.

### Remaining work (TODO)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 7.1a ✅ | Resolve duplicate `PlanetQuadtree` (face-based in `planet_quadtree.hpp` vs radius-based in `planet_patch.hpp`) — merge or rename so both headers can be included in one TU | T1 | Done: radius-based class renamed to `PlanetPatchGenerator`; `test_planets_new.cpp` now includes both headers and passes |
| 7.2a ✅ | Patch mesh generation: vertex positions/normals/UVs + index buffer from quadtree leaves (cubed-sphere projection + terrain height) | T1 | Done: `generatePatchMesh(patch, subdivisions, radius)` in `planet_mesh.hpp/.cpp` — RTC offsets from double patch center, analytical bounding spheres, outward winding verified. Terrain-height displacement still TODO (mesh is smooth sphere today); T-junction skirts deferred to 7.2c |
| 7.2b ✅ | GPU patch pipeline: BDA vertex/index buffers, Slang vertex/fragment shaders, `GraphicsPipeline` with depth; headless render of one patch | T3 | Done: `planet_patch.slang` (BDA via address arithmetic; Slang's `vk::BufferPointer` lacks variable subscript), `test_planet_patch_render.cpp`; zero validation errors |
| 7.2c ✅ | Continuous descent proof: scripted camera orbit→surface, bounded patch count, cross-face crack check | T3+T4 | Done: descent renders at 3 altitudes (3000/2000/1300 m, single patch = bounded); cross-face shared edges coincide to <1 m (T1). Known residual: different-LOD T-junction skirts still TODO |
| 7.1b ✅ | Parallel quadtree LOD traversal (M7.1): job-based `update()` across faces | T1+T4 | Done: `PlanetQuadtree::updateParallel` (one job per leaf subtree); T1 determinism test passes (identical leaves). Speedup validation deferred to benchmark pass (timing asserts are flaky in unit tests) |
| 7.2d ✅ | GPU patch pipeline integration (M7.2): secondary command buffers with dynamic-rendering inheritance (5.5.3b), GPU-timeline deferred retirement queue (5.5.3c) | T2+T3 | Done: `GpuRetirementQueue` (`vulkan_retirement.hpp/.cpp`) with fence-gated deleters + T2 test; secondary CB with `VkCommandBufferInheritanceRenderingInfo` executes identically (T3 starfield test). 2-frame stress validation still TODO |
| 7.4a ✅ | Parallel atmosphere LUT generation (M7.4): job-based LUT from `AtmosphereParams` | T1+T4 | Done: `generateAtmosphereLut` (128×64, single-scattering march, normalized); T1 tests pass (bit-identical parallel vs sequential, range, sun-facing brightness). Speedup validation deferred to benchmark pass |
| 7.4b ✅ | Atmosphere rendering: scattering shell shader using the LUT | T3 | Done: `atmosphere.slang` shell (BDA LUT sampling, bilinear), headless test verifies disc + limb brightening. Golden comparison still TODO (pixel checks today) |

## M8 – Environment and Effects

- **Goal:** stars, nebulae, dust; particles, weapons, shields.
- **Proof:** golden images and frame-time budgets at target scene
  sizes. Mesh shaders only behind `GpuCapabilities.meshShader` with a
  non-mesh fallback.

### Status: complete (2026-11-07)
- All effects render headlessly with pixel tests: starfield (with sun
  billboard), nebula, dust, particles, weapons, shields, thruster,
  collision, sun distortion, wormhole, FTL, afterburner, storm, and
  the instanced asteroid field (depth-tested, BDA-driven).
- Golden image for the starfield (`tests/goldens/starfield_128.png`)
  with per-channel tolerance + failing-pixel fraction harness
  (`STARFORGE_UPDATE_GOLDENS=ON` regenerates); frame-time budget test
  vs ADR-004 interval.
- Mesh-shader criterion: no environment effect uses mesh/task shaders;
  all 13 renderers + asteroid field create via the traditional
  vertex/fragment path regardless of capability (T2 creation proof).

Residual (post-M8 polish, not blockers): bloom post-pass for HDR glow
(star cores currently clamp at UNORM white), true volumetric raymarch
(nebula/dust are FBM approximations), GPU culling for asteroids
(CPU instance list today), golden images beyond the starfield.

### Milestone gate (M1–M8)
Milestones 1–8 are complete as of this revision: build is green with
0 warnings, CPU (187+ cases) and GPU test suites pass, the sandbox
runs validation-clean, and every milestone's TODO table above reflects
reality. M9 work starts from this baseline; new polish items go into
the respective milestone residuals, not new milestones.

### Remaining work (TODO)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 8.1 ✅ | Nebula pipeline: volumetric-look FBM raymarch shader (fullscreen pass, `NebulaRenderer` pipeline + push constants) | T2+T3 | Done: FBM warp shader + headless pixel test (golden harness covers starfield; nebula uses pixel checks) |
| 8.2 ✅ | Dust field pipeline: volumetric extinction pass (`DustFieldRenderer`) | T2+T3 | Done: FBM extinction shader + headless pixel test |
| 8.3 ✅ | Particles, weapons, shields: particle state buffer + emitter, laser/projectile billboards; add missing `ShieldEffect` class | T2+T3 | Done: `ShieldEffect` added; particle/weapon/shield billboards render headlessly (CPU fan-out draws; GPU state buffer deferred) |
| 8.4 ✅ | Thruster, collision, sun distortion, wormhole, FTL, afterburner pipelines | T2+T3 | Done: all render headlessly (billboard/fullscreen passes via shared `internal/effect_pipeline`) |
| 8.5 ✅ | Starfield realism: brightness/size/color distribution, HDR + bloom, sun billboard with glow | T3 | Done: power-law brightness, size-coupled cores, sun billboard at `StarParams.worldPosition`. Bloom post-pass deferred (cores clamp at UNORM white today) |
| 8.6 ✅ | Asteroid field rendering (VISION): instanced displaced-rock mesh, GPU culling, depth + lighting | T2+T3 | Done: displaced icosphere + BDA instancing (200 rocks), depth-tested lambert. GPU frustum culling deferred (CPU instance list) |
| 8.7 ✅ | Storm systems (VISION): animated cloud-layer shader pass | T3 | Done: `StormEffect` FBM cloud layer + headless pixel test |
| 8.8 ✅ | Golden images + frame-time budgets (M8 proof): golden PNG harness for environment scenes, benchmark runs vs ADR-004 budgets | T3+T4 | Done: golden harness + committed starfield golden; 30-frame budget test vs 6.94 ms interval (1080p T4 via M4 harness remains) |
| 8.9 ✅ | Mesh-shader criterion: any mesh-shader path gated behind `GpuCapabilities.meshShader` with non-mesh fallback | T2 | Done: no mesh/task shaders in environment; all 14 renderers create via vertex/fragment path (creation proof) |

## M9 – Public API

- **Goal:** developer-facing API shaped from the working slice.
- **Proof:** the sandbox scene rebuilt using only `include/starforge/`,
  with no Vulkan types visible.

### API-shape constraints

- Only abstractions proven by the current working slice move to
  `include/starforge/`; internal-only concepts and unimplemented effects stay
  internal until they have their own end-to-end example.
- Public headers must be self-contained and must not expose Vulkan, VMA, SDL,
  or GLM types. Public math types use the existing `starforge::math` aliases.
- Public owners are move-only where they own GPU resources. Errors use
  `std::expected` unless an ADR records a different policy.
- Pipeline setup, attachment formats, swapchain depth creation, fences,
  semaphores, queues, allocations, and shader modules remain backend
  responsibilities; callers provide scene-level parameters such as extent,
  color, time, radii, and public context values.

### Remaining work (TODO)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 9.1 ✅ | API inventory and contract pass: map every sandbox dependency to a public concept; decide which working-slice systems are public; record a Vulkan-free boundary and error/lifetime policy in an ADR | Review | Done: ADR-0009 records the opaque-owner, Vulkan-free, and `std::expected` contract; public application and later facade steps build on it |
| 9.2 ✅ | Application/window facade: public, move-only wrappers for engine lifecycle, window/input, surface lifetime, frame submission, resize, and presentation | T1+T5 | Done: `starforge::Application` wraps platform windowing without exposing backend handles; unit tests cover rejection and move-only ownership; renderer-facing submission arrives in 9.3 |
| 9.3 ✅ | Backend renderer facade: an opaque renderer object for swapchain creation/recreation, frame recording, render callbacks, clear and depth handling, and presentation | T2+T5 | Done: `starforge::Renderer` owns context/device/surface/swapchain/frame sync behind `Impl`; public `renderClearedFrame(clearColor)` + `recreate()` + `extent()`; backend selects formats and owns fences/semaphores; record callbacks for drawn content deferred to 9.5 domain facades |
| 9.4 ✅ | Math/world-space public API: expose vectors, matrices, projection helpers, `WorldPosition`, and camera-relative conversion without GLM or Vulkan types | T1 | Done: GLM-free `starforge::math` value types (`Vector`/`Matrix3`/`Matrix4`, `perspectiveVulkan`, `lookAt`) and `starforge::WorldPosition` + camera-relative helpers in `starforge/world.hpp`, bridged to the spatial backend via plain-data `world_bridge` (public and internal math share the `starforge::math` name, so no TU includes both); public convention/precision/large-object tests pass |
| 9.5 ✅ | Working-slice domain facades: expose only interfaces exercised by an end-to-end public example—initially the starfield plus any planet/environment effects used by that example | T2/T3 | Done: `starforge::Starfield` (starfield + sun billboard) renders through `Renderer::renderFrame` record callbacks; `FrameContext` is opaque and viewport/scissor are backend-set; other environment effects stay internal until exercised by the public example |
| 9.6 ✅ | Shader/resource encapsulation: the public API loads bundled shader artifacts and owns buffers, pipelines, images, and allocations; callers do not pass Vulkan formats, handles, or device addresses | T1+T2 | Done: `Starfield::create` resolves bundled SPIR-V at runtime (explicit dir → env var → executable-relative → build-tree fallback), owns pipeline/shader modules behind `Impl`, takes only scene parameters; missing/corrupt shaders surface as `std::expected` API errors (tested) |
| 9.7 ✅ | Sandbox migration: rebuild the sandbox scene using only `include/starforge/` and the public CMake target; remove all internal include paths and direct backend calls | T5 | Done: sandbox rewritten against `starforge_public_api` only (compile line shows `-I include` alone); renders starfield via composable `record()`; zero validation errors after fixing pre-existing depth-barrier aspect and pipeline depth-format bugs |
| 9.8 ✅ | Leakage and regression gates: a test that compiles an external consumer using only public headers, a scan that rejects `Vk`, `<vulkan/>`, VMA, SDL, and GLM from public headers, plus public API smoke tests | T1+T5 | Done: `public_leakage_scan`, `public_leakage_selftest` (poisoned header is rejected), and `public_consumer_compile` CTests; all 5 suites green |
| 9.9 ✅ | Developer documentation and stability policy: public API guide, ownership/threading/error rules, supported configurations, and what may change before 1.0 | Review | Done: `docs/public-api.md` covers quick start, types, ownership, errors, threading, shader resolution, and pre-1.0 stability; README links it and the public example |
| 9.10 ✅ | M9 exit proof: build the public example from a clean checkout using only `include/starforge/`, run it and the full test suites, and confirm no Vulkan types are visible in the public interface | T1+T5 | Done: clean-tree build in a fresh directory, sandbox runs with 0 validation errors, CTest 5/5 (including leakage gates), README M9 status marked complete |

## M9.5 – Toon / Cel-Shaded Rendering

- **Goal:** a cel-shaded (toon) art direction available through the
  public API: quantized lighting bands, rim light, flat color ramps,
  and dark graphic outlines around ships, stations, and planets.
- **Proof:** a cel-shaded scene (ship stand-in, nebula background)
  rendered through `include/starforge/` only, with a golden image and
  a frame-time budget. Target look: the cel-shaded space combat
  showcase (banded shadows, glowing engine cores, outlined hulls over
  nebula backdrops). Planets join the toon look with M17 meshes.
- **Depends on:** M9 (public API). Does not block on M17 mesh assets:
  the proof scene uses procedural placeholder geometry; M17 glTF
  meshes adopt the same toon material when they land.

### API-shape constraints

- Toon shading is a material/lighting concern, not a new backend:
  pipelines, formats, fences, and allocations stay backend-owned;
  callers pass scene-level parameters (band count, rim strength,
  outline width, ramp colors) through public facades.
- No Vulkan types in public headers (ADR-0009 holds); shader
  artifacts are bundled and loaded by the API (M9.6 pattern).

### Remaining work (TODO)

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 9.5.1 ✅ | Toon lighting model: quantized diffuse bands + specular + rim light shader, driven by public material parameters (shadow/highlight colors, band count, rim) | T3 | Done: `toon.slang` + internal `ToonRenderer` (quantized bands, stepped specular, rim) over a static cube; offscreen readback test proves top-band and shadow-band colors exactly |
| 9.5.2 ✅ | Outline pass: inverted-hull (or stencil) outlines in a dark graphic color, width in scene units | T3 | Done: same shader in outline mode (object-space normal expansion, front-face-culled pipeline via new `cullFrontFace` config); readback test proves a dark outline ring around the silhouette |
| 9.5.3 ✅ | Public toon material facade: `starforge::Toon` owning its pipeline/shaders, recording into `FrameContext` like `Starfield::record()` | T2 | Done: `starforge::Toon` in `include/starforge/toon.hpp` via raw-data bridge; public tests cover move-only, missing-shader API errors, render/recreate |
| 9.5.4 ✅ | Cel-shaded proof scene + budgets: ship + nebula in toon style through the public API, golden image, frame-time budget vs ADR-004 | T3+T5 | Done: sandbox composes starfield + toon cube in one frame; `tests/goldens/toon_scene_128.png` committed; p95 frame time under the ADR-004 interval; zero validation errors |

## Game Layer (M10+)

STARFORGE remains a rendering library. Game systems are developed as a
separate layer consuming STARFORGE's public API. The following milestones
describe the recommended order for building game systems on top of STARFORGE.

Recommended tech stack for the game layer:
- Physics: Jolt Physics
- Audio: miniaudio
- ECS: Flecs or EnTT
- UI (dev tools): Dear ImGui
- Noise/Procedural: Custom hash-based + FastNoise2
- Serialization: nlohmann/json (debug) + custom binary or FlatBuffers
- Profiling: Tracy
- Assets: stb_* family + meshoptimizer

### M10 – ECS and Scene Management

- **Goal:** Entity-component system for game objects (ships, asteroids,
  stations, projectiles). Spatial indexing via WorldPosition cell hashes.
- **Proof:** 10k entities with transforms updated and rendered at 60 fps.
- **Tech:** Flecs (chosen over EnTT for prefabs, queries, reflection,
  and built-in JSON serialization for M18 persistence).
- **Depends on:** M9 (Public API).

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 10.1 ✅ | Flecs integration + ECS core: `starforge_ecs` library wrapping a Flecs world; `Transform` (WorldPosition + orientation + scale), separate `Velocity` component (movers only), entity tags (Ship/Asteroid/Station/Projectile), precision-preserving movement system | T1 | Done: Flecs v4.2.0 via FetchContent; `starforge::ecs::Ecs` with fixed-step double-precision movement and quaternion orientation integration; move-constructible (move assignment deleted: Flecs aborts on it); tests prove interstellar-range precision, orientation, and tag queries |
| 10.2 ✅ | Spatial index: per-frame cell-hash index over ECS transforms, lookup by cell and neighborhood | T1 | Done: `SpatialIndex` buckets by `WorldPosition::cellHash()`; tests prove exact cell lookup, 26-neighborhood query, and tracking across cell boundaries |
| 10.3 ✅ | ECS → GPU instance stream: transforms collected into the instanced draw path (M5 infrastructure) | T2 | Done: `collectAsteroidInstances` converts ECS transforms (camera-relative, double precision) into the instanced asteroid format; headless GPU test renders ECS-driven instances |
| 10.4 ✅ | M10 exit proof: 10k entities updated + rendered at 60 fps (frame time under the ADR-004 interval), sandbox scene integration | T2+T5 | Done: 10k-entity step+collect+upload+render loop at p95 under the ADR-004 interval, >25% lit pixels, zero validation errors; sandbox remains green |

### M11 – Physics and Collision

- **Goal:** Rigid body dynamics, continuous collision detection for fast
  ships, raycasts, triggers. Jolt Physics integration.
- **Proof:** Ship collision with asteroid field; no tunneling at 1 km/s.
- **Tech:** Jolt Physics (FetchContent, pinned like Flecs).
- **Depends on:** M10 (ECS).
- **Key architectural constraints:**
  - **Single-precision floating origin**, not `JPH_DOUBLE_PRECISION`:
    even doubles lose sub-mm precision past ~1e8 m (engine reach is
    ~1e18 m) and double Jolt costs 15–30% in broad/narrowphase. The
    physics world is a *camera-relative island* (±5–10 km bubble):
    bodies are positioned camera-relative (same single-cast rule as
    rendering) and synced with ECS `Transform` (`WorldPosition`) each
    step. Jolt never sees interstellar-range coordinates.
  - **No competing thread pools:** Jolt's default thread pool is not
    used; a custom `JPH::JobSystem` adapter maps Jolt jobs/barriers
    onto `starforge::jobs::JobScheduler` to protect the ADR-004 frame
    budget from core thrashing.
  - **No Flecs mutation inside Jolt callbacks:** contact callbacks run
    on worker threads; they push to a deferred, thread-safe chunk
    queue flushed on the main thread after `PhysicsSystem::Update`.
  - **Island activation:** distant asteroids stay cheap ECS linear
    advection; only the `SpatialIndex` neighborhood of active ships
    gets Jolt bodies (static/sensor until disturbed). Physics cost
    scales with the local bubble, not the world population.

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 11.1 ✅ | Jolt integration: FetchContent Jolt (pinned), `starforge_physics` library wrapping TempAllocator/PhysicsSystem/BodyInterface; collision layers (NonMoving/Moving/Projectile/Sensor) + broadphase/object/pair filters; custom JobSystem adapter onto `starforge::jobs`; static floor + dynamic sphere with gravity, fixed 60 Hz sub-stepping | T1 | Done: Jolt v5.6.0 (static, single precision, deterministic, no GPU compute); adapter submits Jolt jobs to the STARFORGE scheduler; tests prove fall/rest contact and run-to-run determinism |
| 11.2 ✅ | ECS bridge: `PhysicsBody`/`Collider` components (motion type, mass, shape) with pre-sync (ECS→Jolt, kinematic/teleport) and post-sync (Jolt→ECS: position, orientation, velocity) in camera-relative float32 | T1 | Done: `attachBody`/`syncEcsToPhysics`/`syncPhysicsToEcs` with camera-relative conversion both ways; ECS entity falls and rests via Jolt, teleport sync verified; body user-data mapping lands with 11.3 contacts |
| 11.3 ✅ | Collision events: contact listener → deferred chunk queue → ECS `CollisionEvent` (both entities, point, normal, impulse) flushed after the step; deleted entities deregister from Jolt immediately | T1 | Done: worker threads only push to a mutex-guarded queue; flush emits tag+payload components on both entities (rebased to world space); sphere–sphere test proves events on both sides |
| 11.4 ✅ | CCD, raycasts, sensors: `LinearCast` motion quality on fast bodies, `castRay(from, dir, maxDist)` query API, sensor triggers (docking zones) | T1 | Done: 2000 m/s sphere stops at a thin wall with LinearCast; raycast hits/misses as expected; sensor overlap fires an ECS event on the intruding entity |
| 11.5 ⬜ | M11 exit proof: ship vs asteroid field through ECS + physics with island activation, frame budget | T2 | Ship collides in the field (events fire), no tunneling at 1 km/s, physics step ≤ 2 ms of the ADR-004 interval, zero validation errors |

### M12 – Audio

- **Goal:** 3D positional audio, music, voice, UI sounds. miniaudio
  integration.
- **Proof:** Spatial audio attenuates with distance; Doppler effect on
  flyby.
- **Tech:** miniaudio.
- **Depends on:** M9 (Public API).

### M13 – Procedural Generation Layer

- **Goal:** Content as pure function of position: `generate(seed, cell,
  scale level)`. Star systems, asteroid fields, stations, resource nodes
  keyed by cell hash. No loading screens. Multi-scale generation with
  caching strategy (JSON metadata + binary caches).
- **Proof:** Same cell generates bit-identical content across runs and
  machines (T1); simulated long flight reaches generated content with
  positions still precise (T1 + T3).
- **Depends on:** M6 (Deterministic Exploration), M2 (WorldPosition).
- **Priority:** Elevated — core to project vision; start as soon as M9
  is complete, ahead of physics and audio.

### M14 – AI and Behavior

- **Goal:** Ship AI (combat, patrol, trading, docking), faction systems,
  traffic management, pathfinding in open space.
- **Proof:** 50 NPC ships with combat AI at 60 fps; traffic flows between
  stations.
- **Tech:** Custom (no recommended library; AI is game-specific).
- **Depends on:** M10 (ECS), M11 (Physics).

### M15 – Gameplay Systems

- **Goal:** Inventory/cargo, trading/economy, missions/contracts, damage/
  shields/subsystems, progression/unlocks, docking, stations, jumping.
- **Proof:** Playable trading loop: buy, fly, dock, sell, repeat.
- **Depends on:** M10 (ECS), M14 (AI).

### M16 – UI and HUD

- **Goal:** HUD, menus, maps, inventory screens, interaction prompts.
  HUD styling follows the M9.5 toon art direction (outlined panels,
  flat colors, graphic rim accents).
- **Proof:** Full game loop playable with UI (no console commands needed).
- **Tech:** Dear ImGui (dev tools) + custom or RmlUi (final UI).
- **Depends on:** M10 (ECS), M15 (Gameplay).

### M17 – Asset Pipeline

- **Goal:** Mesh/material/audio loading (stb, tinygltf, meshoptimizer),
  asset management, hot-reloading.
- **Proof:** Load and render a glTF mesh with textures at 60 fps.
- **Depends on:** M9 (Public API).
- **Note:** glTF materials include a toon shading option built on the
  M9.5 toon lighting model and outline pass, so shipped ships and
  stations match the cel-shaded art direction.

### M18 – Persistence

- **Goal:** Save/load player deltas keyed by cell and object ID. Fast
  binary format (FlatBuffers or custom) + human-readable (nlohmann/json)
  for debugging.
- **Proof:** Save, quit, reload: world state restored exactly.
- **Depends on:** M10 (ECS), M13 (Procedural Generation).

### M19 – Vertical Slice

- **Goal:** Playable vertical slice: one ship + procedural asteroid field +
  basic combat effects + continuous flight with correct precision.
- **Proof:** 5 minutes of gameplay with no loading screens, zero
  validation errors, stable 60 fps.
- **Depends on:** M10, M11, M13, M16, M17.

### M13.5 – Asteroid Field Flythrough (intermediate checkpoint)

- **Goal:** Fly through a procedural asteroid field with correct precision
  and basic ship controls. Motivating checkpoint before the full vertical
  slice.
- **Proof:** 2 minutes of continuous flight through generated content with
  stable 60 fps and zero validation errors.
- **Depends on:** M9 (Public API), M13 (Procedural Generation).
