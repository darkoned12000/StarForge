# Source tree

This directory contains STARFORGE's private implementation libraries. It is
not a separate build or application entry point.

- `core/` — logging and foundational utilities.
- `platform/` — SDL3 window and event handling.
- `vulkan/` — private Vulkan instance, device-selection, and capability code.
- Other subsystem directories are reserved for roadmap work and may be empty.

The supported development instructions and current milestone status live in
the repository-root [README](../README.md). Architecture and verification
contracts are in [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md), and work
order is in [docs/ROADMAP.md](../docs/ROADMAP.md). The sandbox entry point is
`apps/sandbox/main.cpp`; `src/main.cpp` is not a configured build target.
