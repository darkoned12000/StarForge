# ADR-0006: World position representation

- **Status:** Accepted
- **Date:** 2026-11-03
- **Target milestone:** M2.3

## Context

M2 requires a position representation that stays accurate across astronomical
distances. The requirements note (001-world-scale-requirements.md) establishes
that the required extent is at least 1e15 m (large star system) with a design
reach toward interstellar travel (~1e25 m). Near-camera precision must be
sub-millimetre at any distance.

Plain float fails beyond ~1e4 m. Plain double fails beyond ~1e13 m. Neither
meets the required extent with sub-mm precision.

## Options considered

### Option A: int64 millimetres

- **Pros:** exact integer addressing (deterministic), simple, no floating
  point in the storage.
- **Cons:** constant 1 mm spacing (cannot achieve sub-mm), max reach ~9.2e15 m
  (just barely meets the required extent), no scale hierarchy for M6.

### Option B: int64 cell (power of two) + double offset (chosen)

- **Pros:** exact integer cell index (deterministic), sub-mm precision at any
  distance (double within a 2^20 m cell ≈ 0.1 nm), reach ~9.7e24 m
  (interstellar), coarse cell index at any scale is a shift, same design
  supplies the M6 scale hierarchy, one position resolves at every scale.
- **Cons:** more complex than a single integer, requires normalisation when
  crossing cell boundaries, offset must be kept in [0, cellSize).

### Option C: double positions rendered camera-relative

- **Pros:** simplest implementation, no cell arithmetic.
- **Cons:** fails the required extent (double spacing is 12.5 cm at 1e15 m),
  rounding feeds hashes (non-deterministic generation), no scale hierarchy.

## Decision

Use **int64 cell (power of two, 2^20 m) + double offset**. The cell index
gives exact integer addressing for deterministic content generation. The
double offset within the cell provides sub-millimetre precision at any
distance. Coarse cell indices at higher scales are computed by shifting the
cell index (with floor semantics for negative coordinates).

Cell size is 2^20 m (~1.05 million metres), chosen to suit asteroid fields
and allow efficient spatial indexing.

## Consequences

- `WorldPosition` is a value type (copyable, movable) with `fromCellAndOffset`
  and `fromMeters` factories. No RAII needed.
- Arithmetic (`translated`, `shiftedByCells`, `+=`, `-=`) splits displacements
  into cell and sub-cell offset and normalises directly on `(cell, offset)`
  without round-tripping through `toMeters()`, preserving sub-nanometre
  precision at any distance.
- Camera-relative conversion (M2.4) composes positions and full transforms in
  `double` and casts to `float32` once, using the relative vector between two
  `WorldPosition` values.
- Coarse cell indices at higher scales are computed purely with signed integer
  right shifts (`cell >> shiftBits`, floor semantics).
- The cell hash (FNV-1a 64-bit) hashes only integer cell coordinates, providing
  stable, bit-exact keys for deterministic content generation.
- Overflow is detected and reported via `std::expected`. NaN and infinity are
  rejected.
- The representation is CPU-only and Vulkan-free per architecture rules.