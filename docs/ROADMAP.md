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
| 2.0 | Requirements note: required extent, near-camera resolution, determinism, how positions reach a float32 shader, consumers | Review | Note agreed; numbers in it are the acceptance criteria for 2.1–2.5 |
| 2.1 | Characterization tool: reach and spacing of float, double, int64 millimetres, and int64 cell + double offset, from 1e6 m to beyond 1e24 m. Uses no math library | T1 + generated table | Table committed to the note; test asserts naive float error exceeds a stated bound at each distance (proves the problem) and the chosen candidate meets the requirements |
| 2.2 | Math library: GLM wrapped behind STARFORGE aliases (see [ADR-0005](decisions/0005-math-library.md)) and **convention tests**: handedness, up axis, column-major layout, Vulkan Y-down clip space, depth 0..1, reverse-Z (near → 1, far → 0) | T1 | Conventions documented and tested. Library internals are not tested |
| 2.3 | `WorldPosition` (decision note + implementation): power-of-two cells with a local offset, shift-based coarse cell index at any scale (floor semantics), overflow detection, NaN/infinity rejected, stable cell hash | T1 | Round-trip and arithmetic correct at 1e12 m and at the maximum extent; negative coordinates and cell boundaries correct; known cells hash to known values |
| 2.4 | Camera-relative conversion of positions **and full transforms** (compose in double, cast to float once). Includes the large-object case: planet radius 6.4e6 m, camera near its surface, with a per-object local origin | T1 | Error stays within the bound derived from the representation; the large-object case has no vertex jitter at the surface |
| 2.5 | Translation-invariance property tests: random inputs, negative coordinates, cell boundaries, near-overflow. Same relative scene shifted by T from 0 to the maximum extent | T1 | Camera-relative results match within the bound for every T |

Deferred (not scheduled): floating-origin rebase. Rendering is
camera-relative per frame, so a rebase is only needed by float-based
simulation (for example physics). Add it when the first such consumer
exists.

Exit: precision and addressing claims demonstrated with numbers,
before any code stores positions.

## M3 – Geometry and Precision on the GPU

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 3.1 | Offscreen render target, readback, golden-image compare (PNG and tolerance) | T3 | Cleared image matches golden |
| 3.2 | First mesh: vertex buffer, pipeline, Slang vertex and fragment shaders | T3 | Triangle and cube match goldens |
| 3.3 | Camera and reverse-Z with D32_SFLOAT and infinite far plane | T1 (projection math) + T3 | Two objects at a large depth ratio render without z-fighting |
| 3.4 | Translation-invariance proof: same relative scene at origin and at 1e12 m | T3 | Images match within tolerance; a naive-float control run visibly fails |

Exit: vision goal 3 proven end to end.

## M4 – Measure

| Step | Deliverable | Verified by | Done when |
|------|-------------|-------------|-----------|
| 4.1 | GPU timestamp queries, frame-time logging | T2 | Timestamps are non-zero and monotonic |
| 4.2 | Benchmark harness: warmup, N frames, JSON with commit, GPU, driver, build type | T4 | Repeated runs vary by less than a stated percentage |
| 4.3 | CPU-driven baseline: 100k instances | T4 | `benchmarks/baseline.json` recorded (Release) |
| 4.4 | Decision note: Tracy, adopt or not | Review | Note written with reasoning |

Performance budgets are set from the 4.3 numbers, not guessed.

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
