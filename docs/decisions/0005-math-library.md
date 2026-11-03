# ADR-0005: Math library choice

- **Status:** Accepted
- **Date:** 2026-11-03
- **Target milestone:** M2.2
- **Supersedes:** none

## Context

M2 requires vector and matrix types (`Vec3` float/double, `Mat4`) and basic
operations (add, subtract, multiply, dot, cross, normalize, transpose,
inverse). These types must be CPU-only, Vulkan-free, and tested for specific
conventions: right-handed coordinate system, Y-up, column-major storage,
Vulkan clip space (Y-down, depth 0..1, reverse-Z with near → 1 and far → 0).

The project principles require justifying new dependencies and avoiding
unnecessary ones. The math library is a foundational dependency that will be
used throughout the codebase.

## Options considered

### Option A: GLM

- **Pros:** battle-tested, widely used in Vulkan projects, header-only, no
  build system changes, extensive feature set, column-major by default
  (matches Vulkan).
- **Cons:** large header-only library increases compile times, template-heavy
  API surface, includes many features we do not need, swizzle operators can
  produce surprising types, no native support for the cell+offset world-position
  pattern.

### Option B: Minimal project-owned library

- **Pros:** full control over API and implementation, no external dependency,
  tailored to project conventions, smaller compile-time footprint, can include
  only what is needed.
- **Cons:** significant initial implementation and testing effort, must
  handle edge cases (normalization of zero vectors, matrix inversion
  precision) ourselves, risk of bugs in foundational code.

### Option C: GLM wrapped behind STARFORGE type aliases (chosen)

- **Pros:** battle-tested math operations, no GLM types leak into the public
  API (aliases ensure STARFORGE owns the type names), implementation can be
  swapped later without changing consumer code, convention tests verify
  behavior regardless of the underlying library.
- **Cons:** still carries GLM as a dependency (compile-time cost), wrapper
  layer adds a thin indirection, must verify GLM's conventions match our
  requirements (column-major, Y-up, etc.).

## Decision

Use **GLM wrapped behind STARFORGE type aliases**. STARFORGE-owned type names
(e.g. `Vec3f`, `Vec3d`, `Mat4f`) are defined as aliases for the corresponding
GLM types. No GLM headers are included in public STARFORGE headers; only
internal implementation files include GLM directly.

GLM is added as a dependency via CMake `find_package(glm CONFIG)` with a
`FetchContent` fallback, following the same pattern as doctest.

The convention tests (handedness, up axis, column-major layout, Vulkan clip
space, reverse-Z) are written against the STARFORGE aliases, not GLM directly,
so the underlying implementation can be swapped without changing tests.

## Consequences

- GLM is a new dependency. It must be justified and documented (this ADR).
- Public headers expose only STARFORGE type names; GLM types never appear in
  `include/starforge/`.
- Convention tests verify the required behavior; if GLM's defaults do not match
  our conventions, we adjust via GLM's configuration macros or wrap the
  operations.
- If future profiling or maintenance shows GLM is unsuitable (compile times,
  API friction), the implementation can be replaced with a custom library
  behind the same aliases without changing consumer code.
- The math library remains CPU-only and Vulkan-free per architecture rules.

## Alternatives not chosen

- **Pure GLM (no aliases):** GLM types would leak into the public API,
  violating the "Vulkan-free public API" principle (by extension, no
  third-party types in public headers).
- **Pure custom library:** too much risk in foundational code for the initial
  implementation; can be revisited if GLM proves unsuitable.