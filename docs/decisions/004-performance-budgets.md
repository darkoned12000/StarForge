# ADR-004: Performance Budgets

- **Status:** Accepted
- **Date:** 2026-11-04
- **Target milestone:** M4.4
- **Supersedes:** none

## Context

M4 establishes benchmarking infrastructure (timestamp queries, harness, baselines).
M4.4 defines performance budgets: the target frame interval and per-subsystem
allocations that M5+ must meet. Budgets are committed *before* reviewing M4.3
baseline results to avoid fitting targets to existing performance.

## Target frame interval

- **Target:** 144 fps = 6.94 ms maximum frame interval.
- **Statistic:** p95 frame time (not mean). A 6.9 ms mean with 20 ms spikes
  is unacceptable. Also track p99 and max for the most important budgets so
  spikes are visible.
- **Resolution:** 1920×1080 (1080p). GPU time scales with pixel count; 1440p
  and 4K budgets are separate documents.
- **Reference scene:** Submission-stress profile (Profile A) from
  `003-benchmark-scene-spec.md` — 100k instances, ~10% visible, cube mesh,
  camera at (0, 0, 5) looking -Z, seed 42.
- **Headroom rule:** Subsystem allocations sum to at most 80% of the interval.
  The remaining 20% absorbs driver variance, thermal/clock changes, and
  OS scheduling.

## CPU vs. GPU overlap

**Important: these two budgets are NOT added together.** CPU preparation/
submission and GPU rendering **overlap** once multiple frames are in flight.
With 2 frames in flight, the frame interval is approximately the **larger**
of CPU frame time and GPU frame time, not their sum.

Budget the two sides **independently** against the same interval:

- **CPU side:** preparation + submission must fit in the interval, with headroom.
- **GPU side:** all passes together must fit in the interval, with headroom.

"Sync/stall" is **not** an allocation. A stall is a symptom of one side being
the bottleneck. Track it as a **measured stall fraction** (time CPU waits on
a fence, or GPU idles between submissions), with a target near zero for the
bottleneck side.

## CPU-side budgets (per frame, 6.94 ms interval)

| Subsystem | Budget (ms) | Applies to | Scaling | Notes |
|-----------|-------------|------------|---------|-------|
| Frustum culling | 0.30 | total instances | linear | CPU-driven; M5 moves to GPU. 3 ns/instance at 100k assumes early-out + SoA |
| Draw submission (competent) | 0.80 | visible instances | O(1) submission + O(visible) buffer prep | Target after M5. Vulkan draw call is O(1); instance buffer population is O(visible). Naive path is baseline to beat at 2.00 ms |
| Transform composition | 0.20 | visible instances | linear | Double→float cast for camera-relative positions |
| Headroom (20%) | 1.39 | — | — | Absorbs variance |
| **Allocated total** | **2.69** | | | 80% cap is 5.55 ms; 2.86 ms unallocated for future subsystems |
| **Interval** | **6.94** | | | |

## GPU-side budgets (per frame, 6.94 ms interval)

| Subsystem | Budget (ms) | Scaling | Notes |
|-----------|-------------|---------|-------|
| GPU culling (M5) | 0.50 | linear in total instances | Compute shader; placeholder until measured |
| Depth pre-pass (if used) | 0.30 | linear in visible triangles | Optional |
| Main geometry pass | 2.50 | linear in visible triangles | Reverse-Z, D32_SFLOAT |
| Reserved: planets, atmosphere, environment, effects | 0.86 | placeholder | Starting placeholder; will almost certainly need to grow as M7-M8 systems land |
| Headroom (20%) | 1.39 | — | Absorbs variance |
| **Total GPU budget** | **5.55** | | Strictly at 80% cap |

### Scaling expectations

| Cost | Expected scaling | Rationale |
|------|-----------------|-----------|
| CPU submission (naive) | Linear in total instance count | One draw per object |
| CPU submission (competent) | Linear in visible instance count | Culling reduces draws |
| CPU submission (M5 GPU-driven) | ~Flat (constant) | GPU generates draws |
| GPU culling (M5) | Linear in total instance count | Compute shader per instance |
| GPU main pass | Linear in visible triangles | Rasterization workload |
| GPU depth | Linear in visible triangles | Depth testing |

These scaling expectations give M5 concrete pass criteria: CPU submission
should be roughly flat as instance count rises, while GPU culling grows
linearly.

### Reserved subsystem rebalancing rule

When a future subsystem (planets M7, environment M8, effects M8) lands, it
receives an allocation from the reserved block. The total must not exceed 80%
of the interval. The document is revised in the same commit that introduces
the subsystem. If the reserved block is exhausted, headroom must be reduced
or the target frame interval increased.

## Memory budget

- **VRAM budget:** 4 GB maximum allocation on hardware with 8 GB+ VRAM.
  Target hardware-independent: budget for 4 GB cards, not 24 GB cards.
- **Upload bandwidth:** ≤ 100 MB/s sustained host-to-device transfer.
- **VMA statistics:** Allocated bytes, used bytes, allocation count, block
  count, heap usage/budget captured before and after benchmark runs (not
  during timed rendering). Reported separately from frame timing.
- **Memory snapshot vs. profiling:** M4 captures memory snapshots only.
  Full GPU memory profiling (fragmentation, churn) is deferred.

## Frame-pacing rules

- Budgets apply to **offscreen** (headless) rendering. On-screen results
  will differ due to compositor overhead and present pacing.
- Present mode: FIFO (vsync) for sandbox; offscreen for benchmarks.
- Budgets measure GPU execution time and CPU submission time independently,
  not frame interval (which depends on overlap).

## Hardware scope

- **Reference hardware:** AMD Radeon RX 7900 XTX (RADV, Mesa 26.x), AMD
  Ryzen 9 7950X, 64 GB RAM, Linux (EndeavourOS).
- **Numbers apply to:** This GPU, this driver, this CPU, 1080p resolution.
- **Hardware-independent claims:** Only ratios and scaling behavior (e.g.,
  "GPU culling cost grows linearly with instance count", "CPU submission is
  flat as instances rise"). Absolute milliseconds do not transfer.
- **Future reference tier:** A second GPU class must be measured before any
  minimum-spec budget is written. The cross-platform goal in VISION.md must
  not be turned into a performance claim until a second machine is measured.

## Machine-readable budgets

Budgets are also stored in `benchmarks/budgets.json` for use by the compare
tool (4.2c). The compare tool reports "over budget by X ms" as an advisory
warning, not a test gate.

## Exit criteria

- Budget document committed with explicit subsystem allocations.
- Baseline vs. budget comparison committed (one row per budget line with
  status: met / over / unknown, margin, and the milestone that addresses
  each gap).
- Hardware scope documented (RX 7900 XTX development target vs. future
  hardware-independent goals).
- M5 acceptance criteria written as budget lines.