# Source tree

This directory contains STARFORGE's private implementation libraries. It is
not a separate build or application entry point.

- `core/` — logging, version, and foundational utilities.
- `math/` — vector/matrix types (GLM behind STARFORGE aliases) and Vulkan clip-space projection helpers.
- `spatial/` — `WorldPosition` (`int64` cell + `double` offset) and camera-relative transform math.
- `stats/` — percentile and summary statistics for benchmarking.
- `bench/` — benchmark result schema, JSON serialization, and metadata.
- `platform/` — SDL3 window, surface, and event handling.
- `vulkan/` — private Vulkan backend (instance, device, swapchain, frame renderer).
- `gpu_resources/` — VMA allocator, buffers, staging upload, and compute pipeline helpers.
- Other subsystem directories are reserved for roadmap work and may be empty.

The supported development instructions and current milestone status live in
the repository-root [README](../README.md). Architecture and verification
contracts are in [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md), and work
order is in [docs/ROADMAP.md](../docs/ROADMAP.md). The sandbox entry point is
`apps/sandbox/main.cpp`.
