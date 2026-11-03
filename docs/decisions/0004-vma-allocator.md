# ADR-0004: Use VMA for GPU memory allocation

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

GPU resources need centralized allocation, including buffers, images, and
staging resources. A hand-rolled Vulkan allocator would add substantial
memory-type selection, suballocation, and lifetime-management work before it
proves a STARFORGE-specific advantage.

## Decision

Use the Vulkan Memory Allocator (VMA) for GPU memory allocation. Do not build a
custom allocator for the initial resource system. Defer volk; use the current
Vulkan loader approach unless a concrete need justifies changing it.

## Consequences

- Integrate VMA behind the private GPU-resource layer; do not expose it in the
  public API.
- Verify allocation, upload, readback, and shutdown behavior with the M1.5
  headless GPU tests.
- Revisit only if measured requirements or portability constraints show VMA
  is unsuitable.
