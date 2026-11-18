# ADR-0014: Profiling instrumentation and Tracy adoption

- **Status:** Accepted
- **Date:** 2026-11-18
- **Target milestone:** M5.5 (5.5.1e), resolving the M4.4 open "Tracy adoption" decision
- **Supersedes:** none

## Context

M4.0 left Tracy adoption open: "decide using project profiling needs; do not
add it speculatively; wrap in a project macro so it can be compiled out
entirely." 5.5.1e requires fiber and thread zones per task so work-stealing
and per-thread CPU time are visible. The benchmark infrastructure (M4) is a
measurement system and must not depend on a profiler being present.

## Decision

1. **Wrap, do not couple.** All instrumentation goes through `SF_PROFILE_*`
   macros in `src/profiling/profile.hpp`. With `STARFORGE_ENABLE_PROFILING=OFF`
   they expand to nothing, so profiling is compiled out entirely. Benchmarks
   run with profiling compiled out unless specifically investigating CPU work.
2. **Adopt Tracy as an opt-in backend.** `STARFORGE_ENABLE_TRACY=ON` fetches a
   pinned Tracy (v0.13.1) and maps the macros to Tracy scoped zones, thread
   names, and fiber enter/exit. The default is OFF; no profiler dependency is
   required to build or test STARFORGE. GPU timestamp queries remain native
   Vulkan (M4.1c) and are independent of Tracy, so no calibrated-timestamp
   extension is required.
3. **Instrument every task execution path.** The job scheduler emits a named
   `job::execute` zone and a `job::task` logical-fiber scope around each task,
   whether it runs on a worker thread or through the work-helping paths in
   `wait()`/`waitAll()`. Worker threads name themselves at startup.
4. **Keep a dependency-free capture API.** The same macros feed a built-in,
   mutex-guarded event buffer with per-thread zone-time accumulation. This is
   the testable baseline (T1) and remains available when Tracy is disabled.

## Consequences

- No Tracy symbols or headers appear in `include/starforge/`; this is an
  internal backend concern (ADR-0009 boundary holds).
- Tracy is a single pinned FetchContent dependency, built only when enabled.
  Both the default (no profiler) and Tracy-enabled configurations are built
  and tested.
- "Fiber" zones are logical task scopes: the scheduler uses a centralized
  work-helping queue rather than OS fibers. If a fiber-based scheduler is
  adopted later, the same macros map directly onto real fibers.
- Benchmark numbers are never attributed to profiler overhead; profiler and
  measurement remain complementary tools.
