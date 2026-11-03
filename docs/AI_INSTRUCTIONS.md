# STARFORGE AI Development Instructions

You are assisting with development of STARFORGE.

## Project Configuration

- C++26
- Vulkan 1.3 minimum, 1.4 preferred
- Slang shaders
- SDL3
- CMake
- Ninja
- Clang
- Linux initially
- Cross-platform architecture

## Engineering Rules

1. Follow docs/ARCHITECTURE.md and docs/ROADMAP.md.
2. Do not expose Vulkan types in public API headers.
3. Do not introduce unnecessary dependencies. Justify any new one.
4. Prefer modular, maintainable code.
5. Do not create giant monolithic source files.
6. Explain architectural changes.
7. Include error handling.
8. Include tests for new functionality where practical.
9. Avoid breaking existing functionality.
10. Benchmark performance-sensitive changes.
11. Do not assume GPU capabilities.
12. Provide fallbacks for optional GPU features.
13. Do not replace working subsystems without justification.

## Code Conventions

- Fallible construction returns `std::expected<T, std::string>`
  through a static `create()`; do not use exceptions for expected
  runtime failures.
- Every resource is owned by an RAII, move-only type. Do not add
  manual cleanup paths to `main` or `create()` functions.
- Log through `core/log.hpp` with a subsystem tag.
- Query hardware features into `GpuCapabilities`; branch on it.
- Do not add a generic backend abstraction layer (RHI) until a
  second backend exists.
- Do not add headers to `include/starforge/` without discussing the
  API shape first. The public API follows a working vertical slice.
- New subsystems become their own static library under `src/`.
- Add new shaders with `starforge_add_shader(...)` in CMake.

## Development Workflow

- Inspect existing code before modifying it.
- Explain the intended change.
- Make small, focused changes.
- Build after changes.
- Run applicable tests (`ctest --preset debug`).
- Report compiler warnings and errors.
- Update documentation when architecture changes.

## Performance Philosophy

Performance must be measured, not assumed.

Do not claim that STARFORGE outperforms Vulkan without
a repeatable benchmark against a comparable implementation.
