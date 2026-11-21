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

> **Scope and doc authority (review note, 2026-11).** `docs/VISION.md`
> is the authoritative definition of *what* STARFORGE is (a rendering
> library, not a full game engine). This `ROADMAP.md` is the
> authoritative status of *what is done and what remains*. `README.md`
> and `ARCHITECTURE.md` are summaries and must not maintain competing
> architectural descriptions: `ARCHITECTURE.md` still says "M1–M2
> complete" and must be synced (see Doc-sync follow-up at the end of
> this document). AI / physics / audio are game-layer modules built
> *on top of* the public API, not part of the rendering library itself.
> Keep the public API focused — promote an abstraction only after an
> end-to-end consumer exercises it (ADR-0009).

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
| Playable space-game proof           | Player-controlled ship flies continuously through generated space, fights, and trades — no loading screens | M15.0, M19 |
| Production toon showcase            | Authored ship + planet + nebula + asteroids + weapons render in a consistent cel-shaded style (not a cube stand-in) | M17, M19 |

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
| 1.5.1 ✅ | VMA dependency integration | Build | Pinned VMA release available via build; `starforge_gpu` links VMA and Vulkan |
| 1.5.2 ✅ | RAII allocator wrapper (`gpu_resources/allocator.*`) | T1/Build | Allocator created from instance/physical device/device; create/destroy cleanly; no VMA/Vulkan types exposed in public headers |
| 1.5.3 ✅ | Move-only `Buffer` wrapper | T1 | Owns `VkBuffer` + `VmaAllocation`; usage and memory placement are separate concepts; supports mapping, flushing, invalidation; move-only |
| 1.5.4 ✅ | Immediate transfer submission utility (command pool/buffer, one-time submit, fence) | T2 | Headless copy with proper buffer memory barriers (available/visible); queue family internal to Vulkan backend |
| 1.5.5 ✅ | Staging upload and readback test (GPU roundtrip) | T2 | 1 MiB CPU → staging → device-local → readback matches byte-for-byte; deterministic data; uses appropriate memory dependencies |
| 1.5.6 ✅ | Allocation accounting and teardown verification | T2 | All test allocations released before allocator destruction; VMA allocation count zero or accounted for; zero validation errors |

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
| 4.4a ✅ | Budget document: target interval, resolution, reference scene, statistic (p95), headroom rule; CPU-side and GPU-side budgets against the same interval; memory budget; scaling expectations; reserved block for future subsystems with rebalancing rule. Committed before 4.3 results are reviewed | Review | Budget document committed with explicit subsystem allocations (docs/decisions/004-performance-budgets.md + benchmarks/budgets.json) |
| 4.4b ✅ | Baseline vs. budget: one row per budget line with status (met / over / unknown), margin, and the milestone that addresses each gap | T4 | Done: `src/bench/budget_compare.*` + `starforge_bench_report` tool, committed baselines in `benchmarks/baselines/` and report in `benchmarks/baseline-vs-budget.md` (10 rows: 2 met, 0 over, 8 unknown); 9 T1 tests + CTest report-generation gate. Harness no longer emits bogus `gpu_time_ms` when timestamps are unavailable |
| 4.4c ✅ | Hardware scope: numbers apply to this GPU, driver, CPU and resolution; only scaling behavior is claimed hardware-independent | Review | Hardware scope documented |

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

Before sequencing the milestone, five critical items must be addressed.
Items 1–4 are done in Phase 3; item 5 belongs to Phase 4.

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

**Status (2026-11-18):** all five items are implemented — items 1–4 in Phase 3
(in-place fill/barrier/dispatch/barrier sequence, SoA bounds, per-frame double
buffering), item 5 in Phase 4 (`GpuDrivenDraw` selects `vkCmdDrawIndirect` vs
`vkCmdDrawIndirectCount` and BDA vs descriptor storage by capability).

### Phase 1: Foundation & Deferred Hygiene (T1 / CPU Focus)

Address test isolation, CMake configuration, and math contract regressions
before writing GPU culling code.

**Status: complete (2026-11-18).** Loader-free helper extraction in
`vulkan_swapchain_selection.cpp` and `benchmark_workload_scene.cpp`; `starforge_tests`
links no Vulkan/SDL runtime (asserted by the `cpu_tests_no_vulkan_runtime` CTest);
public-API facade tests moved to `starforge_public_api_tests`. Frustum tests use
the engine reverse-Z projection with exact unit-normal and randomized
camera/orientation/scale oracle assertions. Render conventions are gated by
`render_conventions`. Matrix row updated below.

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

**Status: complete (2026-11-18).** `Image` transitions now use tracked layouts
and own RAII views; `Uploader`/`ComputePipeline` recycle one command buffer and
fence per object; queue submissions are mutex-synchronized via `submit2`; and
the GPU test suite covers BDA dereference, 2D/3D dispatch lanes and real
timestamp scaling/nesting. Matrix row updated below.

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

**Status: complete (2026-11-18).** `GpuCulling::recordCulling` now records
`vkCmdFillBuffer` → compute barriers → inline `recordDispatch` → compute→indirect/
vertex barriers into the frame's own command buffer (no CPU map or per-frame
submit). A headless test renders an indirect cube draw from the compacted
visible IDs for two frame slots and checks GPU instanceCount against the CPU
oracle. Matrix row updated below.

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

**Status: complete (2026-11-18).** `test_gpu_cull_parity.cpp` now checks strict
count equality and bit-for-bit sorted index sets across 100/1k/5k/50k instances
at 1%/10%/100% visibility. `GpuDrivenDraw` implements both fallback pairs and
four T2 cases run the full GPU suite with `STARFORGE_DISABLE_FEATURES` forcing
`drawIndirectCount` and `bindless` off. Matrix row updated below.

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

**Status: complete (2026-11-18).** Harness publishes an `instances_drawn`
metric, dumps into `${CMAKE_CURRENT_BINARY_DIR}/benchmarks/baselines`, runs a
low-count validation-enabled case, and asserts competent draws fewer than
naive. `GpuDrivenWorkload` + `BenchmarkHarness` measure 10k/100k/1M with
GPU timestamp scopes; committed baselines and `benchmarks/m5-vs-budget.md`
prove M5 beats the M4 CPU-driven baseline and respects ADR-004.

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
| Phase 1 ✅ | CMake decoupling, unit frustum normals, randomized CPU oracle, viewport Y-flip | `starforge_tests` runs without Vulkan runtime; passes oracle tests | None | Done: swapchain-selection and workload-scene helpers moved into loader-free libraries; `starforge_tests` no longer links Vulkan/SDL (ELF `NEEDED` gate + 321 CPU cases pass); all frustum tests use engine reverse-Z projection; exact unit-normal + randomized camera/orientation/scale oracle; negative-height viewports and CCW gate (`render_conventions`) |
| Phase 2 ✅ | ImageView RAII, ImageUsageFlags, pool/fence recycling, 2D/3D compute & BDA tests | `starforge_gpu_tests` clean under validation; zero allocation thrashing | Phase 1 | Done: tracked `Image` layouts + RAII `ImageView`, cached resettable cmd buffers/fences in `Uploader`/`ComputePipeline` (`operationCount()` tests), synchronized `VulkanDevice::submit`/`submit2`; new BDA-deref, 2D/3D-lane, real N-vs-2N/frame-1/nested timestamp tests. 48 GPU cases green under validation |
| Phase 3 ✅ | Slang culling shader, SoA bounding buffers, double-buffered indirect commands | Valid indirect execution on simple cube mesh | Phase 2 | Done: `GpuCulling::recordCulling` performs GPU-side fill/barriers/dispatch in-frame; `test_gpu_indirect_cull.cpp` renders two frame slots with `vkCmdDrawIndirect`, verifies GPU `instanceCount` against the CPU frustum oracle and lit pixels, validation-clean |
| Phase 4 ✅ | GPU vs CPU culling parity suite, feature fallback implementations | T2 Proof: bitwise matching visible sets; green tests with features disabled | Phase 3 | Done: strict count + sorted-index parity across 100/1k/5k/50k at 1/10/100%; `GpuDrivenDraw` BDA vs descriptor and drawIndirect vs drawIndirectCount paths; 4 fallback T2 cases + full GPU suite CTest runs with each feature disabled (12/12 CTests green) |
| Phase 5 ✅ | 10k/100k/1M benchmark profiles, baseline comparison, validated runs | T4 Proof: 1M instances beat M4; CPU submission time flat | Phase 4 | Done: `GpuDrivenWorkload` + harness measure 10k/100k/1M (CPU submission ≤0.50 ms, 100k GPU cull 0.07 ms, 1M cull 0.60 ms); M4 CPU-driven baselines at 100k/1M committed; `benchmarks/m5-vs-budget.md` + 2 T1 comparison tests; 12/12 CTests |

### M5 review follow-ups

All findings from the M5 testing review and Vulkan API review are resolved in
Phases 1–5 above (see each phase's status note).

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
  proves too time-consuming. Decision recorded in ADR. **Implementation note:**
  the shipped scheduler uses a centralized queue with work-helping; the
  Chase-Lev upgrade remains open (see Outstanding M5.5 follow-ups).

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
| 5.5.1e ✅ | Profiler integration: Tracy/Superluminal fiber and thread zones per task | T1 | Done: `src/profiling/` (`SF_PROFILE_*` macros compile out entirely with `STARFORGE_ENABLE_PROFILING=OFF`), optional pinned Tracy v0.13.1 behind `STARFORGE_ENABLE_TRACY` (default OFF, ADR-0014); `JobScheduler` emits a named zone + logical-fiber scope per task on every execution path (worker loop and work-helping) plus named worker threads; 7 T1 capture tests + a Tracy-enabled build gate |

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
| 5.5.5d ✅ | Parallel secondary command recording | T3 | Done: `src/vulkan/parallel_secondary.*` wires the previously-unused `ThreadedCommandPools` to `JobScheduler` — one secondary per dedicated pool slot recorded concurrently with dynamic-rendering inheritance (`VkCommandBufferInheritanceRenderingInfoKHR`); `test_parallel_secondary.cpp` proves 4 worker-recorded secondaries execute in a primary and match a sequential reference byte-for-byte, with per-slot reset and zero validation errors |

### 5.5.6 Verification & Profiling

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 5.5.6a ✅ | ThreadSanitizer build configuration (tsan_suppressions.txt for Vulkan ICD) | T1/T2 | Build config documented; TSan runs clean |
| 5.5.6b ✅ | Multi-core bit-exact determinism test (content + terrain) | T1 | All thread counts produce identical output |
| 5.5.6c ✅ | Scaling benchmarks (1/2/4/8/16 threads) | T4 | Done: `src/bench/thread_scaling.*` + `starforge_scaling_bench` tool; `benchmarks/scaling/thread_scaling.json` records 1/2/4/8/16-thread medians with bit-identical checksums (4.56× at 16 threads on the dev machine), plus 3 T1 tests and the `scaling_bench_smoke` CTest |
| 5.5.6d ✅ | High-concurrency stress test (1000 tasks, no deadlock) | T1 | Completes; all tasks execute; no deadlock |

### M5.5 Exit Criteria
- Job system operational with a configurable thread count and work-helping
  scheduler (centralized queue; Chase-Lev deques remain an open follow-up).
- Content generation parallelized with bit-identical output.
- Terrain height evaluation parallelized with SIMD.
- Atmosphere LUT computation parallelized.
- DeltaStore thread-safe with cell-partitioned concurrency.
- Zero data races under ThreadSanitizer (with driver suppressions).
- Determinism preserved under parallel execution (bit-exact across thread counts).
- Performance scaling documented in `benchmarks/scaling/thread_scaling.json`
  (monotonic 1→16-thread speedup, bit-identical output). The aspirational
  "4× on 4 cores" figure was not reached on the mixed P/E-core dev machine
  (2.21× at 4 threads); see `benchmarks/scaling/README.md`.
- Thread-local scratch allocators eliminate global heap contention.

### Outstanding M5.5 follow-ups

Resolved and removed from this list: parallel secondary command recording
(5.5.5d), Jolt job-system adapter (M11), miniaudio mixing thread (M12), and
the conditional enkiTS/Taskflow replacement option (M5.5 complete using the
in-house scheduler).

Remaining open items:

- ECS multi-threaded system scheduling — owner: M10. M10 shipped its exit proof
  without parallel system scheduling; `src/ecs` does not submit to `JobScheduler`.
- Chase-Lev work-stealing deques (current scheduler is a centralized mutex
  queue with work-helping) — owner: M5.5.
- `SchedulerConfig::dequeCapacity` is unused pending the work-stealing upgrade — owner: M5.5.
- `SchedulerMode::Deterministic` is declared but not implemented — owner: M5.5.
- Task storage is never reclaimed (unbounded `ownedTasks` growth; needs a
  generational ring buffer) — owner: M5.5.
- Epoch-based deletion uses a fixed 2-frame delay, not full epoch tracking — owner: M5.5.

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
- Quadtree Level-of-Detail (LOD) Management: CPU-based quadtree LOD selection for optimal geometry complexity; continuous descent from orbit to surface with bounded patch count; parallel traversal via `PlanetQuadtree::updateParallel`
- Cubed Sphere Projection: Robust cubed sphere projection utilities for planetary geometry with seamless transitions between cube faces
- GPU Patch Pipeline: BDA vertex/index buffers and `planet_patch.slang` rendering with headless descent/seam proofs
- Atmosphere: `AtmosphereParams`, parallel LUT generation (`generateAtmosphereLut`), and the `atmosphere.slang` scattering-shell renderer

All items formerly listed as deferred (M7.1, M7.2, M7.4) are complete; see the TODO table below.

### Status: complete (2026-11-07)
GPU patch rendering, descent + seam proofs, parallel traversal, and
atmosphere LUT + shell rendering all pass headless tests (see TODO
table above). Residual polish (post-M7) — **elevated to pre-vertical-slice
priority (review, 2026-11):** terrain-height displacement (mesh is
still a smooth sphere), T-junction skirts / LOD seam handling, retirement
stress validation, speedup validations. Rationale: stepped toon lighting
and outlines make even small cracks/normal discontinuities obvious, so
displacement and seam quality gate the final art direction, not just
realism.

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
| 9.5.5 ✅ | Portability + material ramp: keep push constants below Vulkan's guaranteed 128-byte range and support a three-stop diffuse ramp (shadow/mid/lit colors) | T3 | Done: per-draw data lives in a host-mapped BDA buffer (one slot per frame in flight) instead of push constants; push block is 80 bytes; `midColor` alpha=0 derives the midpoint of shadow/base so existing two-color callers keep their look; custom mid tint renders exactly at the ramp midpoint; GPU and public suites stay green |
| 9.5.6 ✅ | **Production toon-showcase acceptance (review follow-up).** M9.5's cube+starfield proof validates the shader but does not prove the game art direction. A separate golden/budget gate is required before M19: authored/placeholder ship with outlined hull + stepped lighting + rim, glowing engine exhaust, nebula background, planet with atmosphere, asteroids with outlines, and a weapon fire/impact effect — all in one composed scene through the public API | T3+T5 | Done: `test_toon_showcase.cpp` composes a chamfered ship hull, emissive exhaust + weapon beam, nebula/starfield backdrop, scattering-rim planet and three faceted outlined asteroids in one headless pass; golden `tests/goldens/toon_showcase_128.png`, p95 < 6.94 ms, zero validation errors. Public composition test records `Toon` mesh + `Starfield`/`Nebula` facades in one `renderFrame` |

### Material architecture (review follow-up — applies to M9.5 and M17)

Do not build seven separate toon shaders for hull/terrain/emissive/shield/
nebula/asteroid/atmosphere. Build reusable material capabilities that
combine: quantized diffuse, stepped specular, rim, emissive tint, fresnel
shield, layered-noise nebula, faceted asteroid, scattering rim gradient.
Ships and hero objects get strong graphic definition (outlines + bands).
Backgrounds (nebulae) stay soft with layered transitions and depth so the
whole frame does not look artificially identical. Planets use stepped
terrain lighting with atmospheric gradients.

**Implementation note:** one `toon.slang` now composes these capabilities via
a bitmask in the BDA draw data (emissive, fresnel shield, faceted, scattering
rim, shadows/attenuation/detail) plus a fourth ramp stop. `ToonMesh` supplies
caller-owned geometry (position / normal / baked outline normal) through BDA,
so the showcase uses a chamfered ship hull, asteroid rocks, a planet sphere
and emissive cones rather than cubes. Additive emissive materials use a third
pipeline; no per-material shaders were added.

### Remaining production toon work (requires later architecture)

| Item | Status | Owner / done when |
|------|--------|-------------------|
| Mesh input, baked outline normals, emissive tint, 4-stop ramp, shadow/attenuation/detail | **Done** in M9.5 — BDA `ToonMesh`, capability bitmask, `toon.slang` branches | — |
| Authored glTF meshes, UVs/tangents, textures/decals and glTF→toon material mapping | **Done** (toon integration): in-house GLB decoder (`src/asset/`), public `starforge::Asset`, embedded PNG decode, UV/tangent retention, `Toon::createMeshFromAsset` + texture capability 32; headless textured-glTF render test. Full M17 asset pipeline (hot reload, LODs, meshoptimizer) remains separate | — |
| Screen-space crease/outline pass | **Done**: `gbuffer.slang` writes object ID + view normals with reverse-Z depth; `edge_detect.slang` post pass linearizes depth and detects ID/normal/depth discontinuities; golden `tests/goldens/toon_crease_128.png` | — |
| HDR/tonemap/bloom and sRGB policy | **Done**: `R16G16B16A16_SFLOAT` HDR target, ACES tonemap to UNORM sRGB-style output, bright-pass + separable Gaussian bloom (`TextureEffect` with sampled-image descriptors); golden `hdr_toon_128.png`, p95 < 6.94 ms, clipped-pixel check | — |
| Stylized environment variants | **Done**: `NebulaParams::celShaded` quantizes FBM into flat bands; the toon-showcase golden now renders a cel-styled sky. Starfield/dust remain photoreal | — |
| Toon HUD/panels/portraits and hero assets | **Done**: `starforge::Hud` public facade + `HudRenderer` draw outlined flat panels, a portrait frame and accent bars (`hud.slang`, golden `toon_hud_128.png`); hero meshes are covered by the glTF→Toon path above and composed in the public showcase test | — |

## Game Layer (M10+)

STARFORGE remains a rendering library. Game systems are developed as a
separate layer consuming STARFORGE's public API. The following milestones
describe the recommended order for building game systems on top of STARFORGE.

Recommended tech stack for the game layer:
- Physics: Jolt Physics
- Audio: miniaudio
- ECS: Flecs (chosen — prefabs, queries, JSON serialization for M18)
- UI (dev tools): Dear ImGui
- Noise/Procedural: custom integer-hash for authoritative content only;
  cosmetic shader effects (animated nebulae, smoke, dust turbulence)
  may use floating-point noise (M13 ADR-0012; FastNoise2 is not used for
  authoritative world state — see M13 constraints).
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
| 11.5 ✅ | M11 exit proof: ship vs asteroid field through ECS + physics with island activation, frame budget | T2 | Done: ship flies through a 200-rock ECS field; island activation attaches local bodies, collision events fire, traveled > 5 m, p95 step+sync ≤ 2 ms, zero validation errors |
| 11.6 ⬜ | **Physics-island boundary stress test (review follow-up).** Ship crossing a physics-island boundary while colliding: bodies transfer without position jumps, velocities preserved, contacts not duplicated, no bodies left in the old island, collision events remain valid through the transfer | T1 | Deterministic crossing test passes at boundary; no teleport/jitter, no duplicate contact events |

### Deferred scene-submission API (review follow-up)

The public API today is low-level (each facade records into `FrameContext`
manually). When a consumer needs hundreds/thousands of objects, introduce
a scene submission model (`scene.add(ship/planet/asteroidField/station);
renderer.render(scene, camera)`) — illustrative, not a prescribed API.
Do not build it until a working example needs it (ADR-0009 rule).

### M12 – Audio

- **Goal:** 3D positional audio, music, voice, UI sounds. miniaudio
  integration.
- **Proof:** Spatial audio attenuates with distance; Doppler effect on
  flyby.
- **Tech:** miniaudio.
- **Depends on:** M9 (Public API).
- **Key architectural constraints:**
  - **Headless-capable device:** the audio layer works without audio
    hardware via `ma_backend_null` (null backend). All tests run on
    the null backend; the sandbox/games use the OS default backend.
  - **Camera-relative spatialization:** emitter/listener positions
    are camera-relative float32 (same single-cast rule as rendering
    and physics). miniaudio never sees interstellar-range coordinates
    (`WorldPosition` → `toCameraRelative` → float32).
  - **No Flecs mutation on the audio thread:** miniaudio runs its own
    mixing thread. Triggers flow one-way via a deferred, thread-safe
    queue flushed on the main thread (same pattern as M11's
    `ContactQueue`). TLS RNG pitch variation is the only
    non-deterministic runtime use (M5.5 design notes).
  - **No miniaudio types in the public API:** `include/starforge/`
    stays miniaudio-free per ADR-0009 (extended in 12.5); the internal
    layer exposes `ma_*` types like M11 exposes Jolt types.
  - **Asset loading deferred to M17:** M12 proves playback with
    procedurally generated tones; file streaming, hot-reload, and
    glTF audio belong to M17 (Asset Pipeline).
  - **Post-M12 gap fill (2026-11):** internal `SoundPool` for overlapping
    one-shots, `SoundBank` file streaming/load, fade control, and richer
    procedural waveform synthesis (sine/square/saw/triangle/noise, sweep,
    envelope) landed in the audio engine; the public bridge exposes
    file streaming and fade. Remaining gaps (public voice-pool facade,
    DSP/effects node graph, memory-backed streaming/data-source
    abstraction, mixing polish) are tracked in M20.

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 12.1 ✅ | miniaudio integration + core context: FetchContent miniaudio (pinned 0.11.26), `STARFORGE_USE_MINIAUDIO` option, `src/audio/` → `starforge_audio` library wrapping `ma_context`/`ma_engine`; RAII `AudioContext` (move-only, `std::expected` factory, master gain); `ma_backend_null` path for headless CI; ADR-0010 | T1 | Done: miniaudio v0.11.26 via FetchContent; `starforge::audio::AudioContext` creates on the null backend with no audio hardware, rejects invalid config as expected errors, destroys cleanly; tests prove move-only RAII and master-volume clamping |
| 12.2 ✅ | 2D core playback + buses: `SoundBank` (via `SoundBank::create` factory, per the error-handling convention) owning decoded `ma_sound` voices from in-memory bytes (WAV/FLAC/MP3 decoders) with play/stop/loop/volume/pitch; per-bus `ma_sound_group` routing (Master/Music/SFX/UI/Voice) with independent bus gains; retained encoded bytes per sound (decoders read lazily, so caller buffers must not be the lifetime source); one-shot replay rewinds at EOF; UI/Music are non-spatial, SFX/Voice spatial; pure-CPU procedural tone generator (`generateToneWav`) for fixtures — no binary assets committed | T1 | Done: `SoundBank::create` returns expected errors (no zombie banks); `loadFromMemory` decodes WAV, retains bytes (UAF regression tested via source-buffer destruction), rejects malformed/empty data; play/stop/loop/volume/pitch and EOF-rewind replay verified on the null backend; bus gains independent, Master routes to engine gain; UI/Music spatialization disabled (tested); tone generator produces canonical WAV with verified peak and header |
| 12.3 ✅ | 3D spatialization: pure-CPU attenuation/pan/Doppler reference math (T1 property tests) in `src/audio/spatial.hpp` (inverse-distance-clamped gain with positive floor at max range — matching OpenAL/miniaudio, which clamps rather than silences; stereo pan; Doppler pitch factor with supersonic saturation); miniaudio listener/emitter API (`AudioContext::setListenerPose`, `SoundBank::setEmitterPose` + `setEmitterAttenuation`/`setEmitterDopplerFactor`, positional/velocity getters); camera-relative conversion helpers (`toCameraRelativeFloat`, `cameraRelativeVelocity`) enforcing the single-cast rule | T1 | Done: attenuation monotone with distance and floor at max range; pan sign follows horizontal position; Doppler rises on approach and falls on recession (flyby peak at closest-approach approach-speed max), supersonic closure saturates at 2.0 (never inf/NaN); emitter/listener poses round-trip through miniaudio; emitters at 1e12 m world position give sane camera-relative params |
| 12.4 ✅ | ECS bridge + threading: `AudioEmitter`/`AudioListener` components in `src/audio/bridge.hpp` (+ `registerAudioComponents`, `attachEmitter` with attenuation forwarding to miniaudio, `registerAudioEmitterCleanup` stopping voices on entity destruction); per-frame `syncAudio` (Transform/Velocity → camera-relative poses via `toCameraRelativeFloat` → engine listener and emitter voices); deferred thread-safe `TriggerQueue` (any thread pushes, main thread flushes into `SoundBank::playWithVolume` — retriggering while playing, triggered gain = base volume × event volume without clobbering the base; no Flecs mutation on worker/audio threads) fed by `pushCollisionTriggers` (sub-threshold impulses < 1.0 ignored, volume clamped to [0.05, 1.0]) | T1 | Done: sync tracks listener (position/velocity/direction) and emitter poses; emitter velocity follows ECS Velocity; collision CollisionEvent+above-threshold impulse plays the entity's one-shot through the queue; feather-touch (0.2 impulse) is silent; trigger does not clobber configured base volume; queue is mutex-safe from producer threads; dead entity IDs skipped on flush; destroyed emitters stop their voice (no ghost loops) |
| 12.5 ✅ | Public facade + M12 exit proof: `include/starforge/audio.hpp` move-only facade (no miniaudio types leak); `public_leakage_scan` extended to reject `ma_` symbols and miniaudio includes; sandbox flyby scene (looped tone, emitter crossing the listener — attenuation + Doppler audible); `docs/public-api.md` Audio section + README status | T1+T5 | Done: `starforge::Audio` (move-only, `std::expected` factory, camera-relative `setListener`/`setEmitter`, `generateToneWav` fixture) built through the raw-data `audio_bridge` (public TU never includes internal or GLM math); leakage gate rejects `ma_` symbols (self-test green); public-consumer compile covers `audio.hpp`; sandbox runs the flyby silently-or-audibly (Default backend, falls back to silence); full CTest suite green |

### M13 – Procedural Generation Layer

- **Goal:** Content as pure function of position: `generate(seed, cell,
  scale level)`. Star systems, asteroid fields, stations, resource nodes
  keyed by cell hash. No loading screens. Multi-scale generation with
  a bounded, process-level raw cache. Disk-backed JSON/binary caching
  is deferred to M18 (Persistence), so M13.5 is the in-memory tier, not
  persistent storage.
- **Proof:** Same cell generates bit-identical content across runs and
  machines (T1); simulated long flight reaches generated content with
  positions still precise (T1 + T3).
- **Depends on:** M6 (Deterministic Exploration), M2 (WorldPosition).
- **Priority:** Elevated — core to project vision; start as soon as M9
  is complete, ahead of physics and audio.
- **Design stance:** M13 is not another generator algorithm milestone. The
  pure generator exists (M6); M13 is the operational streaming layer
  that makes a moving camera survivable at 60 fps: traversal, budgets,
  GPU-ingestion caps, and a bounded multi-tier cache. Toon/cel art
  direction is a first-class constraint, not a styling pass applied
  after generation.

### Key architectural constraints

- **Bit-exact generation without FP SIMD noise:** no FastNoise2 for authoritative
  procedural content. Cel art deliberately avoids high-frequency fractal noise;
  procedural features use integer-hash lattices, macro-facets, clean geometric
  plates, and step-like crater features. Micro-detail belongs to stylized
  decals/hatch fields, not geometric noise. Deterministic integer RNG +
  `-ffp-contract=off` (already project-wide) keeps cell output identical
  across x86-64/ARM64; SIMD floating-point noise would not. Cosmetic shader
  effects (animated nebulae, smoke, dust, particles) may use conventional
  floating-point noise — they do not feed authoritative world state
  (ADR-0012).
- **Toon LOD transition policy:** inverted-hull outlines amplify pops.
  LOD switches need hysteresis bands plus dithered crossfade or
  silhouette-preserving geomorphs; outline rendering must ignore
  fading-in duplicate geometry (otherwise thick ink lines double/ghost).
- **Chunk-seam lighting:** cel quantization makes small normal errors
  visible as hard seams. Neighboring cells must share boundary normals
  via analytic derivatives or unified boundary stencils — seam normals
  are a bit-exact contract, not a rendering tune.
- **GPU ingestion budget:** background CPU buffers are useless if the
  main thread blows the 6.94 ms interval on staging and descriptor
  writes. Per-frame upload gets a hard cap (≤ 2 MB staging or ≤ 32
  registered draw calls per frame, whichever the benchmark says);
  queuing never defers into the measured frame.
- **Three-tier cache, owned separately:**
  `Seed/Descriptor` (transforms, seeds, bounds) → `GPU buffer`
  (vertex/index) → `ECS instance pool` (ephemeral simulation entities).
  The generator owns an immutable, keyed raw store; the scene/streaming
  director (game layer) owns `Evicted → Loaded in RAM → Mounted in
  Flecs/Vulkan` transitions. No layer knows another's eviction policy.
- **Deltas are a sparse overlay, never a mutation:** final world state
  is `ApplyDelta(BaseChunkContent, DeltaMap[cell])` evaluated at
  instance time. Base chunks stay byte-identical for checksum tests and
  reset/respawn debugging; writer-side mutation would poison that.

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 13.1 ✅ | **Decision record (ADR-0011):** pipeline contract (`generate(seed, cell, level)` as the sole entry), integer-hash/fixed-point noise policy with explicit cross-platform bit-exactness gate, chunk-boundary normal contract, wall-clock + upload budget allocation from ADR-004 | Review | Done: ADR-0011 records noise policy, seam-normal contract, and byte/draw-call caps; indexed in decisions/README |
| 13.2 ✅ | **Traversal hardening:** deterministic chunk ordering per camera position, `StreamingTraversal` hysteresis cursor (retain cells inside the hysteresis band, no boundary oscillation); **toon's LOD rule** — `ToonLodHysteresis` discrete switches inside the hysteresis band with dithered crossfade amount and outline switched to the dominant (`outlineLevel()`) mesh at 50% | T1 | Done: replaying a path delivers each cell exactly once in order; stream keeps a dead band (no toggle on jitter); LOD crossfade arms inside the band, cancels on reversal, and its outline switches at 50%; overlapping threshold bands are clamped to spacing; tests cover all of these |
| 13.3 ✅ | **CPU + GPU budget enforcement:** per-level object caps (`GenerationBudget::objectCaps`, deterministic truncation), measured per-chunk wall-clock (`generateContentTimed`), **per-frame upload cap** (`UploadBudget` all-or-nothing staging bytes + draw calls before recording) | T1 + T4 | Done: caps enforced by tests; staging/draw estimates derive from chunk object count; budget violation fails the unit gate; timing field recorded for the T4 JSON harness wiring (benchmark-harness integration deferred to the flythrough proof) |
| 13.4 ✅ | **Parallel generation:** `generateContentParallel` extended with an in-flight batch cap (waves of `maxInFlightBatches`, no queue flood), bit-identical to sequential at any thread count | T1 + T4 | Done: wave cap verified against sequential on the neighborhood + LOD bands; thread-count determinism already covered by the job-determinism suite |
| 13.5 ✅ | **Bounded raw-generation cache:** `RawGenerationStore` (`seed → ContentChunk`) with O(1) LRU splicing (iterator stored per entry, no heap churn on hits), LRU eviction by entry count or retained bytes, hit/miss/eviction counters, mutex-guarded concurrent readers; shared immutable chunks handed to callers | T1 | Done: replay path hits after first miss, eviction stays under budget, generated chunks are shared so held references stay valid |
| 13.6 ✅ | **M13 exit proof + M13.5 flythrough:** 2 simulated minutes at 1,000 m/s through a generated asteroid field — `StreamingTraversal` + `RawGenerationStore` instantiate/destroy ECS asteroids through per-cell groups, peak upload bytes/draws tracked against `UploadBudget`, precision held across a level-0 boundary; asteroids carry quantized orientations so toon light bands vary per cluster; render proof carried by the existing headless GPU asteroid suite (zero validation errors) | T1 + T3 | Done: 7,200-frame simulated flythrough crosses its first cell boundary, both cells generate once and hit on replay, entity pool stays bounded by the two-cell cap, and the headless render suites remain validation-clean |
| 13.7 ✅ | **Player deltas overlay:** `DeltaMap` read-time overlay via `applyDeltas` + ECS instantiation helpers (`instantiateChunk`/`unmountChunk` in `starforge_ecs`); save/load serialization round-trip byte-exact | T1 | Done: overlay never mutates the base chunk (checksum-identical after save/load); removed/modified/added deltas mount correctly; round-trip apply is byte-identical |
| 13.8 ✅ | **M13 full exit:** simulated 2-minute flight at 1,000 m/s anchored past 1e12 m — `StreamingTraversal` + `RawGenerationStore` + ECS instantiation with precision intact and deterministic cell hashes | T1 + T3 | Done: interstellar flight test reaches generated content >1e12 m from origin, mounts correct chunks, and matches fresh direct generation byte-for-byte; headless GPU suites remain validation-clean |

Order note: 13.6/13.7 (deltas + full exit) deliberately follow the 13.5
flythrough — the vertical slice M19 needs the field to stream flawlessly
first, persistence second.

**Status: M13 is complete (13.1–13.8 ✅). 13.5 is not remaining work.**
The bounded `RawGenerationStore` cache, simulated 2-minute flythrough,
delta overlay, and >1e12 m interstellar flight are all implemented and
tested. M12/M13 deliberately remain internal-only: no public streaming
facade has been added, because ADR-0009 promotes public APIs only after
an end-to-end consumer. The next planned work is M14 (AI and Behavior).

### M14 – AI and Behavior

- **Goal:** Ship AI (combat, patrol, trading, docking), faction systems,
  traffic management, pathfinding in open space.
- **Proof:** 50 NPC ships with combat AI at 60 fps; traffic flows between
  stations.
- **Tech:** Custom (no recommended library; AI is game-specific).
- **Depends on:** M10 (ECS), M11 (Physics).

### Phase A – behavior model

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 14.1 ✅ | **Decision and component model (ADR-0013):** fixed-tick AI scheduler, component layout (`AiAgent`, `Faction`, `NavigationTarget`, `Health`), determinism and threading rules | Review | ADR records the component model, think schedule, and the rule that AI decisions are computed on the main/Flecs thread while Jolt callbacks stay deferred |
| 14.2 ✅ | **AI components and spatial queries:** `AiAgent` state/target/speed profile, faction relation lookup, neighborhood pickup through `SpatialIndex`, camera-relative presence checks | T1 | Create 50 agents; nearest hostile and friendly filters return deterministic, expected entities within the configured perception radius |
| 14.3 ✅ | **Steering controller:** seek, arrival braking, orbit/dogfight, separation, heading alignment; writes ECS `Velocity` and orientation only | T1 | A seeker from standstill decelerates before a target and points its ship forward; separation pushes two nearby agents apart |

**Status: Phase A complete (14.1/14.2/14.3 ✅).** Component model, perception,
and steering are implemented and tested; the FSM, traffic, combat, and scale
proof remain phases B and C.

### Phase B – traffic and movement

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 14.4 ✅ | **Finite-state behavior:** Patrol, Transit, Docking, Engage, Evade, and Retreat states with hysteresis/timers | T1 | State transitions fire in sequence without rapid oscillation near thresholds |
| 14.5 ✅ | **Navigation and traffic management:** station-to-station routes, waypoint queues, dock slots, separation, priority, and deterministic deconfliction | T1 | A multi-ship traffic test reaches dock markers without collision events and repeats with the same positions |
| 14.6 ✅ | **Physics island coupling:** kinematic NPC ships attach through M11's `PhysicsBody`, synchronize with `syncEcsToPhysics`, and read back through `syncPhysicsToEcs` | T1 + T3 | NPC ships obey AI velocity, collide with asteroids, and keep WorldPosition precision at >1e12 m |

**Status: Phase B complete (14.4/14.5/14.6 ✅).** Behavior FSM, station
routes/traffic slots, and kinematic physics coupling are implemented and
tested; scheduling/perf and the combat exit proof remain Phase C.

### Phase C – combat and scale

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 14.7 ✅ | **Combat interaction:** target acquisition, Jolt `castRay` line-of-sight, range checks, weapon cooldown, minimal damage events and audio triggers | T1 | Hostile enters range → acquires and fires; ally enters range → ignored; line-of-sight block prevents a shot |
| 14.8 ✅ | **Schedule and perf guard:** staggered think intervals, per-tick agent budget, and deterministic decision order | T1 + T4 | 50 agents tick every frame at 60 fps in the benchmark harness; rerun produces identical decisions |
| 14.9 ✅ | **M14 exit proof:** 50 NPC ships combining patrol, station transit, combat, and traffic in one simulated run | T1 + T4 | Traffic reaches stations, combat engages and evades, collisions are resolved, p95 AI+physics step stays inside the ADR-004 interval, and results remain deterministic |

**Status: Phase C complete (14.7/14.8/14.9 ✅); M14 complete.** `src/ai/combat`
adds `Weapon` cooldown/damage, Jolt `castRay` line-of-sight verified by
Flecs user data (self-body safe origin, blocker-aware), and
`DamageEvent`/`CombatFireEvent` for damage and audio triggers.
`src/ai/scheduler` uses a timer-based due queue with a per-tick budget and
roll-over (no starvation). `tests/test_ai_exit_proof.cpp` runs two deterministic 600-frame,
50-ship simulations (25 merchants routing to a station, 25 pirates) to
identical final positions, with engagements, damage events, and p95 frame
time inside the 60 fps budget.

Scope note: AI evolves entirely in the game layer (`src/ai` or equivalent) and stays internal until a public consumer justifies a facade (ADR-0009). Gameplay inventory/economy/missions remain M15; toon HUD work is M16; persistence of AI identities is M18.

Known M14 follow-ups for later milestones (intentionally out of scope):
- 0-HP ships persist as wrecks — M15 needs destruction handling and shields.
- Patrol holds position (no waypoint loops yet) and weapons are hitscan
  only — no projectiles/spread/visual tracers.
- Entity handles inside `AiAgent`, `NavigationTarget`, and `DamageEvent`
  are runtime-only; M18 persistence must remap them on load.
- `FactionRelation::Allies` and `AiAgent::trafficPriority` are reserved
  hooks, not yet wired to behavior.
- Perception is O(n) per think over one 2^20 m world cell (fine at
  current scale); a finer AI grid is later work if battles grow.
- Station route approach is always the station's +Z side regardless of
  origin; heading has no explicit up vector and can roll over long
  turns. Both are visual polish, deferred past M15.
- Faction/economic simulation (trader → pirate interception → security
  response → cargo economy): do not build a galaxy-scale simulation
  yet. Keep to convinced small-scale simulation first (owner: M15 for
  the economic hook, M19 for the interconnected proof).

### M15.0 – First Flight (intermediate playable demo, before full M15)

- **Goal:** a complete playable spaceflight demonstration so engine work
  from M10–M14 is proven as a game, not only as tests and benchmarks.
  This comes *before* the full trading-loop scope of M15: fly first,
  economy second.
- **Proof:** the player can control a spaceship continuously through a
  procedural asteroid field, approach/orbit a planet with atmosphere,
  see a nebula backdrop, encounter one AI hostile, fire weapons with
  stylized impact/shield effects, and experience the full toon art
  direction — with no loading screens during the demonstration.
- **Depends on:** M10 (ECS), M11 (Physics), M13 (Streaming), M14 (AI),
  M9.5 (Toon). May use placeholder ship geometry; the authored-asset
  upgrade to a Blender/glTF hero ship updates this scene in place.
- **Acceptance:** mouse/keyboard flight, procedural fields streaming,
  weapons + shields, toon look (outlined hull + glowing engines),
  60 fps at the ADR-004 interval, zero validation errors. Recorded as
  a reproducible sandbox/game-layer scene; screenshots/goldens
  reviewed for visual identity (not just pixel counts).
- **Where it lives:** new game-layer executable `apps/game/` (not the
  public-API sandbox). Rendering goes through the public API
  (`Application`/`Renderer`/`Starfield`/`Toon`/`Audio` + `WorldPosition`);
  ECS/physics/AI/audio internals are linked as game layer. It is the
  integration test for a week of engine work: if it does not feel like
  a game, gaps are filed as deferred items, not as engine redesigns.
- **Non-goal for this milestone:** economy, missions, trading, stations,
  persistence. Those keep M15 from becoming a second-engine project.
- **Control scheme (Freelancer-style, fixed for M15.0):**
  - `W` — set/increase thruster power. Speed *persists*; you do **not**
    need to hold `W` to keep moving. Thruster setting is a state value
    (e.g. 0–100%), changed by discrete `W`/`S` taps.
  - `S` — set/decrease thruster power; speed falls toward 0.
  - `A` — barrel roll left (roll about forward axis).
  - `D` — barrel roll right.
  - Mouse move — steer: push forward (up) = nose up (pitch up), pull
    back (down) = nose down (pitch down), left/right = yaw left/right.
    Mouse is *steering*, not camera-orbit. Camera is a chase camera.
  - Left mouse button — fire primary guns (hold to keep firing).
  - Right mouse button — fire secondary weapon (per-press trigger).
  - `Space` — toggle flight mode ↔ UI mode. In UI mode the mouse cursor
    is free to click UI elements; in flight mode the mouse steers.
  - `P` or `Esc` — pause; shows a menu overlay.

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 15.0.1 ✅ | Game shell: `apps/game/` main loop — fixed-step simulation (60 Hz) decoupled from render, resize/minimize handling, clean shutdown | T5 | Done: `starforge_game` target, 60 Hz accumulator with 5-step cap, resize/format-changed recreate, minimize sleep, `waitIdle` shutdown; committed on `gamedemo` |
| 15.0.2 ✅ | Player ship + input + camera: Freelancer scheme above (`W`/`S` persistent throttle set-points, `A`/`D` barrel roll, mouse steer, LMB/RMB weapons, `Space` flight/UI toggle, `P`/`Esc` pause menu), ECS `Transform`/`Velocity` ship, chase camera, all positions via `WorldPosition` → camera-relative | T1+T5 | Done: public input facade (`Key`/`MouseButton`/`MouseMotion` on `Application`, SDL polling in `platform::Window`); `apps/game/ship_controller.hpp` (persistent throttle, steer, roll, mode/pause, weapon states) + `game_bridge` (raw ECS bridge, no TU mixes public+internal math); `test_game_controls.cpp` (6 T1 cases) passes; manual window run still to be confirmed by the user |
| 15.0.3 ✅ | Procedural field integration: `StreamingTraversal` + `RawGenerationStore` + `instantiateChunk` driving ECS asteroids, toon/instanced rendering, zero stalls | T1+T3+T5 | Done: `GameWorld` owns stream/store/budget; `updateStreaming()` each frame instantiates/unmounts chunks (startup: 5 chunks, 307 rocks mounted); ship spawns near the 2^20 boundary so full throttle crosses into cell 1 in ~26s with no loading screen; visible rocks (nearest 64 within 2.5 km) rendered as camera-relative toon cubes; HUD logs cell + mounted count each second. Arrow-key held steering (Up/Down/Left/Right) now works alongside mouse for headless/no-mouse play. Manual crossing run still to be confirmed by the user |
| 15.0.4 ✅ | Environment showcase: planet patch + atmosphere shell, nebula backdrop, starfield — all composed in one frame, toon-consistent | T3+T5 | Done: public `starforge::Planet` facade (`planet_sphere.slang` ray-sphere disc writing projective `SV_Depth`, toon diffuse bands, Fresnel rim `1-(n·v)^p`; `planet_atmosphere.slang` additive limb), composed with `Nebula`, `Starfield` and `Sun` in `apps/game/main.cpp`; golden `tests/goldens/planet_ray_sphere_128.png` (`test_planet_render.cpp`), validation clean |
| 15.0.5 ✅ | Combat slice: one M14 AI hostile (engage/evade), weapon fire, shield hit, health/damage events, audio trigger through M12 facade | T1+T5 | Done: `apps/game/game_simulation.hpp/.cpp` drives engage/evade hostile, primary/secondary weapons with cooldowns, shield→hull damage and `GameEvent`s; hits/stun/death trigger `starforge::Audio` one-shots (`generateToneWav`); hostile drawn as a fresnel-shield toon mesh; T1 proof `test_game_simulation.cpp` |
| 15.0.6 ✅ | Integration + budgets gate: 5-minute flight proof, frame-time p95 < ADR-004 interval, screenshot/golden review | T4+T5 | Done: `test_game_simulation.cpp` runs a 5-minute (18k-step) simulation with p95 < 6.94 ms; all GPU goldens (planet, rock field, HDR, crease, HUD, nebula 3D) render under validation with zero errors; CTest 12/12; screenshots/goldens visually reviewed |
| 15.0.7 ✅ | Sun as visitable celestial body (Grok review): replace the directional sun billboard with a real mesh using M7 quadtree LOD (LargeObjectSetup patch origin), emissive core shader with limb darkening (`1 - u(1-μ)`), corona as additive sphere at 5–10× radius, near plane set to 0.01× radius. Coexists with the starfield sky sphere | T3+T5 | Done: `SunRenderer` (dual-pass patch cubed-sphere core + corona shell via `LargeObjectSetup`), `sun.slang` + `sun_corona.slang`, public `starforge::Sun` facade, `test_sun_render.cpp` (Eddington darkening T1 proof + headless GPU T3 proof), billboard intensity set to 0.0f in `apps/game/main.cpp` with 3D celestial sun at ~80 km |
| 15.0.8 ✅ | Asteroid variety (Grok review): switch the demo from cubes to `generateAsteroidMesh` (displaced UV-sphere), 3–5 base mesh variants selected per seed, per-instance color tint, tumbling already in place | T1+T3 | Done: seeded `generateAsteroidMesh(radius, lat, lon, seed)` produces distinct deterministic variants (`test_rock_field.cpp` T1); public `starforge::AsteroidField` uses a lumpy base mesh with per-instance procedural shape variation and a four-colour palette; golden `tests/goldens/rock_field_128.png` shows varied lumpy rocks with crisp outlines |
| 15.0.9 ✅ | Asteroid instancing + LOD (Grok review): single `vkCmdDrawIndexed` over instance storage, cheaper impostors at long distance, distance-aware outline width | T4 | Done: `RockFieldRenderer` issues one `vkCmdDrawIndexed` for near rocks (plus one inverted-hull outline draw with CPU-derived distance-aware width) and one `vkCmdDraw(6, N)` impostor draw for distant rocks; 4096-instance capacity, no pop at the cull radius; golden + budget proof in `test_rock_field.cpp` |

**Exit (M15.0):** a playable demo a stranger can run: fly, explore,
fight, look at the toon art direction — and a list of gaps filed for
M15/M17, not silently absorbed.

#### First Flight Deferred Polish (review 2026-11, code review)

Recorded so they are not lost; each item names its owner milestone:

| Item | Why later | Done when |
|------|-----------|-----------|
| Planet ray-sphere billboard + atmosphere limb | — | **Done**: `planet_sphere.slang` writes `SV_Depth` + Fresnel rim `1-(n·v)^p`; `planet_atmosphere.slang` additive limb; public `starforge::Planet`; golden `planet_ray_sphere_128.png` under validation |
| Instanced asteroid draw (single `vkCmdDrawIndexed`) | — | **Done**: `RockFieldRenderer` one instanced mesh draw + outline + impostor tiers; 4096-capacity `starforge::AsteroidField`; golden `rock_field_128.png` |
| Nebula half-resolution offscreen target | — | **Done**: `NebulaOptimized` renders FBM into a 1/2×1/2 (¼-pixel) target, ≥75% fillrate cut by construction; `test_nebula_optimized.cpp` p95 < 6.94 ms, deterministic parity, golden `nebula_noise3d_128.png` |
| 3D noise volume texture (32³/64³) | — | **Done**: seeded 64³ `R8_UNORM` volume uploaded via BDA-free copy; `nebula_noise3d.slang` samples it instead of ~300 hash evals/px; `Image` supports 3D types/views; golden pixel test + determinism proof |
| Nebula 2D→3D noise (octahedral fold seams) | Done 2026-11 (nebula.slang now 3D FBM on the sphere) | seamless golden pixel test across rotations |
| Skybox border / fold lines | Octahedral projection removed in the 3D-noise switch; no fold edges remain | golden pixel test across rotations and translations |

### M15 – Gameplay Systems

- **Goal:** Inventory/cargo, trading/economy, missions/contracts, damage/
  shields/subsystems, progression/unlocks, docking, stations, jumping.
- **Proof:** Playable trading loop: buy, fly, dock, sell, repeat.
- **Depends on:** M10 (ECS), M14 (AI), M15.0 (flight).
- **Procedural ≠ gameplay-locked (review follow-up, layered generation):**
  generated systems must also support deliberate placement. Layers, in order:
  procedural foundation → designer-authored overrides (story locations,
  unique stations, special encounters, mission objectives) → faction and
  economic placement → mission-specific additions → persistent player
  modifications (M13 deltas + M18). Procedural content must never dictate
  gameplay; authored content must be able to win over generated content.
  Player modifications to preserve: mined-asteroid depletion, destroyed
  stations, discovered/visited locations, collected resources, ownership
  and faction changes (see M18).

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 15.1 ✅ | Damage/shields/subsystems + destruction: shield pool, hull, subsystem hits, 0-HP wreck resolution (closes M14 follow-up) | T1 | Done: `src/gameplay/damage.hpp/.cpp` (`DamageState`, `applyDamage` shield-first with pierce, `tickDamage` recharge gated by shield subsystem, engine/weapon/shield efficiency, wreck flag); T1 proofs in `test_gameplay.cpp` and `test_game_simulation.cpp` (wreck stops the hostile; events drive audio) |
| 15.2 ✅ | Cargo/inventory + stations: shared cargo model, station stock, buy/sell, docking approach + dock slots (extends M14 traffic) | T1+T5 | Done: `src/gameplay/economy.hpp/.cpp` (`CargoHold`, `Market`, `EconomyState::buy/sell` as sparse overlay — no base-chunk mutation) + `docking.hpp/.cpp` (approach/dock slots/speed gate); exposed through `GameSimulation` with UI-mode dock/buy/sell keys in `apps/game/main.cpp`; T1 trading-loop proof |
| 15.3 ✅ | Missions/contracts + progression: mission offer/accept/complete, credit reward, one unlock gate (e.g. jump range) | T1+T5 | Done: `src/gameplay/missions.hpp/.cpp` (define/offer/accept/deliver/complete) + `progression.hpp/.cpp` (idempotent unlocks mapping to jump range); `GameSimulation` applies rewards as credits + unlocks; T1 proof covers accept → deliver → complete → credit + `jump2` unlock |
| 15.4 ✅ | Authored-override layer + jumping: designer-placed station/encounter wins over generated content in its cell; jump to a new system with streaming intact | T1+T3 | Done: `src/gameplay/authored.hpp/.cpp` layered generation (authored kinds suppress generated, player Depleted/Destroyed mods survive regeneration) + `jumping.hpp/.cpp` (range-gated system transition); T1 proofs in `test_gameplay.cpp` and `test_game_simulation.cpp` |
| 15.5 ✅ | M15 exit proof: trading loop end-to-end (buy → fly → dock → sell → repeat) in the `apps/game/` demo | T5+T4 | Done: `test_game_simulation.cpp` drives the exact `GameSimulation` used by `apps/game` — dock → buy 5 ore → undock/fly → dock → sell → repeat, plus mission/jump; 5-minute gameplay p95 < 6.94 ms; full CTest 12/12 with zero validation errors; goldens visually reviewed |

### M16 – UI and HUD

- **Goal:** Flight HUD, targeting, navigation, communications, inventory,
  station interaction, menus, maps, interaction prompts.
  HUD styling follows the M9.5 toon art direction (outlined panels,
  flat colors, graphic rim accents).
- **Proof:** Full game loop playable with UI (no console commands needed).
- **Tech:** Dear ImGui (dev tools) + custom or RmlUi (final UI).
- **Depends on:** M10 (ECS), M15 (Gameplay), M15.0 (flight HUD is the
  first consumer; the full HUD expands from it).
- **Plan:** detailed breakdown in [M16_PLAN.md](M16_PLAN.md).

**Tech decision (confirmed 2026-11):** the shipped UI uses a **custom toon
canvas** built on `starforge::Hud`/`hud.slang` (already matching the M9.5
art direction), with a built-in 8×8 ASCII bitmap font for text. Dear ImGui
stays dev-tools only, as the roadmap specifies. **Fallback: RmlUi** if the
custom canvas cannot deliver the required UI functionality — revisit the
decision if any step below proves unachievable. Commits follow one-per-step
(16.1–16.10) in strict order.

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 16.1 ✅ | Absolute mouse position + UI input primitives: `Application::mousePosition()` and mouse-button press/release edges, independent of relative-motion steering | T1 | Done: `MousePosition` + pure-CPU `MouseButtonEdges` in `starforge/input.hpp` (T1 `test_public_input_edges.cpp`); `Application::mousePosition/Pressed/Released` wired from `Window::mousePosition()` (SDL3 float coords) and sampled in `pollEvents()`; live smoke in `test_public_application.cpp`; flight steering untouched |
| 16.2 ✅ | Bitmap font + text rendering: built-in ASCII glyph table (pure CPU), internal text renderer, public `UiCanvas::addText()` | T1 + T3 | Done: `src/gameplay/ui_text.hpp` (5×7 glyphs 32–126 + `textQuads`, T1 `test_ui_text.cpp`); `UiRenderer` + public `starforge::UiCanvas` facade record one BDA draw with alpha blend and matching depth format; golden `tests/goldens/ui_text_128.png` + public facades smoke, zero validation errors |
| 16.3 ✅ | Toon UI canvas: `addRect`/`addBar`/`addPanel`/`addText`/`clear`/`record` in one retained-mode draw with outlined panels, flat fills, rim accents | T3 + T4 | Done: pure widget geometry in `src/gameplay/ui_shapes.hpp` (T1); `UiCanvas` queue expands widgets+glyphs into one BDA draw (`ui_text.slang`, alpha blend, depth matched); composite panel golden `ui_panel_128.png`, public facade smoke (panel/bar/rect/text), zero validation errors |
| 16.4 ✅ | UI state machine + hit-testing/focus: screens (Flight/Pause/Inventory/Station/Comms/Map/Menu), focus stack, widget hover/press, click hit test, keyboard nav | T1 | Done: `apps/game/ui_state.hpp/.cpp` pure-CPU `UiState` (per-frame widget registration, topmost-wins hit test, click edges, id-keyed focus that survives rebuilds, screen stack with Flight base); T1 `test_ui_state.cpp` covers hover/click/overlap/focus/screens |
| 16.5 ✅ | Flight HUD + targeting: throttle/speed, shield/hull bars, credits/cargo, crosshair, target cycle (Tab), target info, lock bar, faction colour coding | T1 + T3 | Done: pure-CPU `TargetingState` (id-preserving contact rebuild, next/prev cycle, held-trigger lock on in-range hostile) + T1 `test_targeting.cpp`; live `UiCanvas` flight HUD with panels/bars/text/crosshair/target bracket in `apps/game/main.cpp` (Tab cycles, `Key::Tab` through input stack); golden `ui_panel_128.png` extended with reticle; zero validation errors |
| 16.6 ✅ | Navigation/radar: nearby rocks, hostile, planet, station markers with bearing/distance, range rings, selected destination + ETA | T1 + T3 | Done: pure-CPU `ui_radar.hpp` projection (ship-relative → disc, bearing, range cull) + T1 `test_ui_radar.cpp`; game draws bottom-right radar panel with 2 km rings, colour-coded contacts (rock/hostile/planet/station), player dot and `TGT ... BRG ...` readout; golden `ui_panel_128.png` extended with radar rings/dots; zero validation errors |
| 16.7 ✅ | Communications (chat): message log for combat/trade/mission events + player text input with keyboard capture in UI mode, channels with colour coding | T1 + T3 | Done: pure-CPU `ui_comms.hpp/.cpp` (TTL ring buffer, text entry) + T1 `test_ui_comms.cpp`; `Application::takeTextInput()` from SDL text events, Return/Backspace keys; game pushes combat/trade/mission/jump messages and draws a UI-mode comms panel with colour-coded channels; golden `ui_panel_128.png` extended with comms lines; zero validation errors |
| 16.8 ✅ | Inventory + station interaction: cargo list with quantities, station market buy/sell by click (replaces M15 arrow keys), docking state/prompt in UI | T1 + T3 | Done: pure-CPU `ui_station.hpp/.cpp` (panel geometry + click→action mapping) + T1 `test_ui_station.cpp`; game registers station buttons with `UiState` hit-testing and draws a hover-tinted panel with credits/cargo/dock status; legacy Hud panels removed; radar enlarged to 240 px with +/- zoom; compact comms log always visible; golden extended with station buttons; zero validation errors |
| 16.9 ✅ | Menus, maps, prompts, toasts: pause menu, galaxy & sector maps with current location/jump range, contextual interaction prompts, toast queue replacing console logs | T1 + T3 | Done: pure-CPU `ui_map.hpp/.cpp` (projection + jump reachability) + T1 `test_ui_map.cpp`; `M` toggles a sector map with reachable dots, toasts render top-centre from a TTL queue, pause menu shows controls; comms log doubles as the docking/progress prompt; golden extended with map/toast primitives. Also fixed the UI vanishing after a kill: `UiRenderer` now grows its vertex buffer instead of silently dropping the frame (T3 regression `large text draw survives buffer growth`); radar contacts rotate into the ship frame (T1 180-turn test) |
| 16.10 ✅ | Full-loop UI proof + budget gate: launch → fly → fight → dock → buy/sell → mission → jump → repeat entirely through UI; 5-minute run, p95 < ADR-004, zero validation errors, screenshots reviewed | T4 + T5 | Done: `test_m16_exit_proof.cpp` drives the full loop through `UiState` hit-testing + `applyStationAction` (repeatable buy/sell, mission/unlock/jump) and a 5-minute p95 < 6.94 ms gate; T4 busy-HUD golden/budget test in `test_ui_render.cpp`; goldens visually reviewed; all 16.x steps ✅ |

**M16 UI polish (post-exit, 2026-11-19):** user review fixes — comms panel
is now always visible bottom-left (combat alerts appear only as top-centre
toasts); station panel is centred with roomy buttons; `T` locks the nearest
hostile and a box tracks the hostile on screen with a direction chevron;
radar threat dots are larger/pulsing; pause menu has a real Quit button;
sector map is clickable with system selection, a JUMP HERE button, clamped
labels, drill-in object list and a pulsing current-system marker; the NAV
readout is labelled.

**M16 UI polish round 2 (2026-11-19):** the sector now spawns five travelling
NPCs (a mix of hostile and neutral ships) so targeting can be tested against
moving, non-attacking contacts; player weapons can be aimed at the selected
NPC by id. `T` now toggles lock on/off and the game starts with no target.
The pause menu fits its labels inside the buttons, and choosing Sector Map
closes the menu and opens an immediately-interactive chart. The map was
redesigned as a sci-fi star chart with a starfield, nav grid, dashed range
rings and a dedicated info column separated by a glowing divider; current
position is green, selected destination yellow, joined by a dotted route, and
the action button reads `Jump to <name>`. The NAV readout moved above the
radar, Cargo sits on its own HUD line, the radar gained a rotating sweep and
a periodic ping, and panel accent strips were thinned so they no longer touch
titles. Verification: CTest 12/12, 378 CPU cases, zero validation errors.

**M16 UI polish round 3 (2026-11-19):** comms keeps a permanent 200-line
scrollable history (PageUp/PageDown or wheel) with an always-visible chat
input box — Enter toggles it, send chats to sector traffic with canned NPC
replies until a chat AI is wired. Each system now has a distinct environment
theme (nebula, planet, sun colours/size) plus a HUD system readout, so jumps
are visibly different. Fixed the haul-quest deadlock: alpha-centauri is now
inside the base jump range and the jump uses the real chart distance; quest
status is shown in the station panel. Radar hostiles are now plain red dots
without the lock-on-style ring; a yellow bracket marks the selected target
so an untargeted radar has no "lock" indicator. Verification: CTest 12/12,
382 CPU cases, zero validation errors.

### M17 – Asset Pipeline — COMPLETE (2026-11-19)

- **Goal:** Mesh/material/audio loading (stb, tinygltf, meshoptimizer),
  asset management, hot-reloading, LODs, material variants, texture
  streaming.
- **Proof:** Load and render a glTF mesh with textures at 60 fps — the authored
  hero ship (three textured, multi-material parts) renders in First Flight and
  in a headless GPU proof with zero validation errors; LOD and streamed-mip
  selection are exercised in the live loop.
- **Depends on:** M9 (Public API). **Priority note (review, 2026-11):**
  bring forward alongside M15/M15.0 — procedural geometry is not a
  substitute for authored ships, stations, capital ships, weapons,
  cockpits, and structures. Placeholder cube geometry cannot prove the
  art direction. Blender + glTF is the starting combination.
- **Note:** glTF materials include a toon shading option built on the
  M9.5 toon lighting model and outline pass, so shipped ships and
  stations match the cel-shaded art direction.
- **Deliverables to include:** hero ship mesh, station mesh, LOD chains,
  material variants per the M9.5 material-capability model, texture
  streaming, asset hot-reloading, glTF material → toon material mapping.

**M17 implementation summary (commits a3cbca3..1a59a55):** M17.1 CPU asset
manager (typed handles, path dedup, refcounts, reload queue); M17.2 polling
file watcher + hot-reload coordinator; M17.3 extended material decoding and
glTF PBR → toon capability mapping with four named variants; M17.4
meshoptimizer v0.25 vertex weld/optimise/simplify plus asset LOD chains with
hysteresis; M17.5 mip-chain generation, budgeted LRU texture residency, public
`StreamingTexture` and `Toon::createMeshFromAsset` mip upload; M17.6 external
`.gltf` (file + data URIs) and file-based audio loading; M17.7 authored
`hero_ship.glb`/`station.glb` generated deterministically by
`tools/generate_assets.py`; M17.8 full game integration and GPU proof.

Design decisions: the in-house nlohmann/stb decoder is extended rather than
replaced by tinygltf v3 (a C library with no CMake target that would duplicate
the GPU-proven decoder); textures stay in the toon BDA packed-RGBA8 path, so
streaming means mip generation/residency and on-demand GPU upload, not a new
sampler pipeline. All asset internals are CPU-only and the game consumes them
through the public `starforge::Asset` facade.

**Comic-ink art pass (2026-11-20, external review feedback):** the authored
assets are now multi-part starfighter / station models (chamfered lofted
fuselage, swept bevel wings with crimson stripes, tinted canopy, armoured
engine housings with recessed glowing plasma cores; station spire, dual
habitat rings, trusses and window bands) with 128–256 px procedurally inked
panel/AO textures. `toon.slang` gained bilinear BDA texture sampling with
wrapping, ink-masked specular/rim highlights and texture-modulated emissive
cores; the proven specular/rim intensities were retained so cel bands stay
crisp and HDR tonemapping does not flatten (the HDR golden was refreshed for
the small resulting difference).

**Visual follow-up (2026-11-20):** the station is now anchored at the ship's
spawn X (~1047 km) instead of the world origin, so it is actually visible
ahead of the ship; the planet shader gained comic-ink coastlines, stepped
bands and an inked terminator (golden refreshed); armour albedo is darker so
ink seams read instead of washing out, close meshes stay on mip 0, and
specular/rim are damped on bright textured armour. Comms lines and the chat
input are clamped to 32 characters so text cannot escape the panel.
Screen-space Sobel inking, HDR bloom and billboard/ribbon particles remain
deferred. Verification: Debug + Release CTest 12/12, zero validation errors.

**Visual refinement pass 2 (2026-11-20, user feedback):** the ship's white
nose/canopy fans now sample inked albedo instead of bright plate centres and
the canopy glint is dimmer; the planet uses layered value-noise fbm with
ridged mountain chains and inked rock seams; the station's emissive window /
beacon materials are opaque (no additive no-depth artifacts) and its six
radial trusses are continuous beams that connect hub, rings and docking pods.
NPCs are low-poly oriented fighters instead of cubes; player tracers are aimed
at the selected target, enemy fire spawns red incoming tracers, and the shield
flash is a faint additive spherical shell instead of an opaque blue cube
(golden refreshed for the new planet terrain). Verification: Debug + Release
CTest 12/12, zero validation errors.

**Gameplay/visual pass 3 (2026-11-20, user feedback):** Space no longer opens
the trade screen; approaching the station within 1 km shows a clickable
`<DOCK>` / `<CHAT>` prompt above Comms, and docking opens a reworked station
panel with Trade / Missions / Navigation sections (Esc or Close exits).
Engine nozzles are near-black recessed circles; the toon sun direction is now
rotated into view space so world-anchored station lighting no longer shifts
with the camera; station windows are brighter yellow/green with slowly
blinking tip beacons. The target box now applies the ship model matrix so it
sits where aimed lasers actually travel. Laser bolts collide with asteroids,
damage them, and spawn fragment sparks on destruction. Verification: Debug +
Release CTest 12/12, zero validation errors.

**Gameplay/visual pass 4 (2026-11-20, user feedback):** restored Space as the
cursor-free UI mode needed to click the proximity dock/chat prompt; station
tip beacons are now large, clearly blinking **red** navigation lights. NPC
fighters are scaled up and brightly coloured so they can be seen, with a
ship-local key light. Weapons now always fire down the main crosshair while
the T-selected target square acts as a steering/lead guide. Ship shading uses
a fixed ship-local key light and the station uses the world sun rotated into
view space, so neither shifts colour with flight direction. Verification:
Debug + Release CTest 12/12, zero validation errors.

**Combat usability pass (2026-11-20, priority feedback):** fixed the target-box
frame (NPC positions are camera-relative world offsets, so the extra ship-model
rotation was pushing the box off the ship); added per-contact diamonds with
range and a large off-screen "TARGET - TURN" arrow; hostiles now steer toward a
standoff point in the player's forward arc and move slower so they can be
tracked. Lasers fire down the crosshair with 600 m/s / 720 m range and hit
asteroids (three demo rocks sit ahead of spawn so this is testable), dealing
12 damage per hit with fragment sparks. NPC fire tuned to 8 damage / 1.5 s with
2/s shield regen for survivable combat. Station UI split into Trading and
Missions tabs. Verification: Debug + Release CTest 12/12, zero validation errors.

### M18 – Persistence

- **Goal:** Save/load player deltas keyed by cell and object ID. Fast
  binary format (FlatBuffers or custom) + human-readable (nlohmann/json)
  for debugging.
- **Proof:** Save, quit, reload: world state restored exactly.
- **Depends on:** M10 (ECS), M13 (Procedural Generation).
- **Must persist (review follow-up):** mined asteroid depletion,
  destroyed stations, discovered locations, collected resources,
  mission-related world changes, ownership/faction changes, and the
  M15 authored-override layer. Implementation order: generated base
  chunk (M13) + delta overlay + authored overrides + save file. AI
  entity handles must be remapped on load (see M14 follow-ups).

### M19 – Vertical Slice

- **Goal:** Playable vertical slice: one ship + procedural asteroid field +
  basic combat effects + continuous flight with correct precision.
- **Proof:** 5 minutes of gameplay with no loading screens, zero
  validation errors, stable 60 fps.
- **Depends on:** M10, M11, M13, M15, M15.0, M16, M17.
- **Pre-slice gates (do not close M19 without these):**
  - Production toon showcase (9.5.6) rendered and reviewed, not the
    cube proof.
  - Terrain-height displacement + LOD seam handling (M7 residual)
    sufficient for orbit-to-surface descent in toon lighting.
  - HDR / tonemap / bloom decision and pass without flattened bands
    (M9.5 deferred work).
  - Perf-profile discipline: budgets split per subsystem (geometry,
    shadows, outlines, planet, nebula, post, particles, UI) so no
    single effect consumes the full 6.94 ms ADR-004 interval, and
    rendering profiles per hardware tier rather than tuning only for
    the RX 7900 XTX dev machine.
  - Physics-island boundary stress (11.6) and doc-sync follow-up
    (below) closed or explicitly deferred with owners.

### M20 – Audio Engine Fidelity

- **Goal:** Close the remaining M12 audio gaps so gameplay audio
  (laser/thruster/boost/missile/radar-lock, ambience, music) can be
  authored and mixed without reaching around the engine. Builds on the
  internal `SoundPool`, file streaming, fade, and procedural waveform
  synthesis already landed after M12.
- **Proof:** Public API can create a pooled SFX voice, play overlapping
  one-shots, stream a long file, fade a voice/bus, and route through at
  least one DSP effect node (e.g. low-pass or reverb send) on the null
  backend with T1 tests; no miniaudio types leak through
  `include/starforge/`.
- **Depends on:** M12 (Audio), M17 (Asset Pipeline file/audio loading).

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 20.1 | Public voice-pool facade: expose pooled overlapping one-shot playback through the public audio bridge/API (wrapping internal `SoundPool` or an equivalent voice allocator) with handle/volume/bus semantics consistent with `starforge::Audio`. | T1 | Public consumer can preload a pool and trigger overlapping one-shots without direct access to internal audio headers; pool exhaustion behavior is deterministic and documented. |
| 20.2 | DSP/effects node graph: wrap miniaudio node-graph effects behind an internal/public-safe API (filters, reverb/EQ as needed, per-bus or per-voice sends). Keep ADR-0009 boundary intact. | T1 | At least one effect (e.g. low-pass filter or reverb send) can be attached/configured through engine API on the null backend; effect params are clamped/validated; no `ma_*` symbols in public headers. |
| 20.3 | Memory-backed streaming/data-source abstraction: support streaming from a caller-provided data source or memory-backed stream in addition to file-path streaming, for packaged assets and generated audio. | T1 | A streamed voice can play from a non-file data source abstraction on the null backend; lifetime/seek/read contracts are explicit and tested. |
| 20.4 | Audio mixing polish: bus-level fade/helpers, effect-send convenience, and documentation/tests covering the laser/thruster/boost/missile/radar-lock gameplay scenarios. | T1 | Representative gameplay SFX scenarios are covered by tests/examples using only supported engine APIs; docs updated. |

### Checkpoint: Asteroid Field Flythrough (superseded by steps 13.6 and 13.8)

- **Status:** covered by the completed M13 steps above; do not treat the
  old "M13.5" heading below as a separate remaining milestone.
- **Original goal:** fly through a procedural asteroid field with correct
  precision and basic ship controls as a motivating checkpoint.
- **Proof:** the 2-minute, 1,000 m/s, 7,200-frame simulated flights in
  steps 13.6 and 13.8, including the >1e12 m interstellar flight and
  the headless GPU render suites.
- **Scope note:** the proofs simulate kinematic flight; player-controlled
  ship handling belongs to the game layer, not the content-streaming
  milestone. The player-controlled counterpart is M15.0 (First Flight).

## External review follow-ups (2026-11, prioritised backlog)

Source: ChatGPT repo review (two passes, Nov 2026). Nothing here reopens
completed milestones; each item names its owner milestone so it is not
forgotten. Review verdict: architecture, large-world precision, Vulkan
foundation, public API, and procedural direction are sound — do not
restart or redesign. The priority is to prove playability and the toon
art direction.

| Priority | Item | Owner |
|----------|------|-------|
| P0 | Doc sync: update `ARCHITECTURE.md` ("M1–M2 complete", renderer/render-graph "roadmap structure for M3+") to describe the current implementation through M14; clarify in `VISION.md`/README that STARFORGE is the rendering library and M10+ is the game layer on top. `VISION.md` = what it is, `ROADMAP.md` = status. | Docs (do before M15) |
| P0 | First Flight playable demo (M15.0): player ship + asteroid field + planet + nebula + hostile AI + weapons/shields + toon look, no loading screens. Fly before full economy. | Game layer (M15.0) |
| P0 | Production toon showcase gate (9.5.6): ship with outlines/rim, engine glow, nebula, planet atmosphere, asteroids, weapon effect in one scene — golden + budget + visual review. The cube proof is shim validation only. | M9.5 → M17/M19 gate |
| P0 | Toon material architecture: reusable capabilities (bands, rim, emissive, fresnel, layered nebula, faceted, scattering) combined per material class, not seven separate systems. Keep backgrounds soft vs. outlined hero objects. | M9.5/M17 |
| P1 | Planet terrain displacement + LOD seam / T-junction handling (M7 residual) — gates toon look at the surface; small errors are obvious under stepped lighting + outlines. | M7 residual → M19 gate |
| P1 | Authored system layer on top of procedural generation (M15): designer overrides, faction/economic placement, mission additions. Procedural must not lock out story locations. | M15 (built on M13) |
| P1 | Asset pipeline brought forward alongside M15 (M17): glTF hero ship/station, LODs, material variants, texture streaming, hot-reloading. | M17 |
| P1 | HDR / tonemap / bloom + emissive pipeline decision (M9.5 deferred work) before authoring final palettes. | M9.5/M8 → M19 gate |
| P2 | Physics-island boundary crossing stress test (11.6). | M11 |
| P2 | Flight HUD + targeting/nav/comms (M16), expanded from M15.0's flight HUD. | M16 |
| P2 | Performance profiles: per-subsystem budgets + per-hardware-tier rendering profiles, not only the RX 7900 XTX target. | M4.4 / M19 |
| P2 | Persistence of player world changes (M18): depleted asteroids, destroyed stations, discovered locations, faction/ownership, authored overrides. | M18 |
| P2 | Scene submission API (M11 deferred note): design only when a consumer needs it. | Post-M19 / when motivated |
| P3 | Faction/economic simulation at small scale first (trader/pirate/security/cargo) — not a galaxy sim. | M15 → M19 |
| P3 | AI polish already tracked under M14: patrol waypoints, projectiles/tracers, finer perception grid, station approach side, up-vector roll control. | M14 follow-ups |

**Standing principles from the review (kept, not new):**
- Generated world = `Generate(seed, location) + PlayerChanges`; base
  chunks stay byte-identical, player changes are a sparse overlay (M13.7).
- Floating-point noise is fine for cosmetic shader effects (animated
  nebulae, smoke, dust); authoritative generation stays integer-hash
  deterministic (ADR-0012).
- Resist adding graphics features just because Vulkan supports them;
  every new system must serve the intended game or solve a demonstrated
  engine limitation.
- Every milestone keeps its proof discipline: sandbox scene or test
  with a measurable pass criterion, build stays green.
