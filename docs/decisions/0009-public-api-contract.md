# ADR 0009: Public API Contract

- **Status:** Accepted for M9 implementation
- **Date:** 2026-11-07
- **Supersedes:** none
- **Extends:** ADR-0003 (shape the public API after a working vertical slice)

## Context

Milestones 1–8 produced a working Vulkan renderer, world-position system,
M7 planet rendering, and M8 environment effects through internal `src/`
interfaces. M9 turns the proven slice into a developer-facing API without
exposing the Vulkan implementation.

The inventory for the initial public surface is:

| Internal capability | Public concept for M9 | Exposure decision |
|---|---|---|
| SDL window/event handling | `starforge::Application` | Public in 9.2 |
| Vulkan instance/device/swapchain/renderer | `starforge::Renderer` | Public in 9.3; internal handles stay private |
| STARFORGE math aliases and projection helpers | `starforge/math.hpp` | Public in 9.4 |
| `WorldPosition` and camera-relative transforms | `starforge/world.hpp` | Public in 9.4 |
| Starfield and any effects used by the rebuilt example | Opaque effect/scene handles | Public in 9.5 |
| Queues, fences, semaphores, VMA, shader modules, captures, and device addresses | Backend-owned resources | Internal indefinitely |
| Benchmark/GPU-only test utilities | Benchmark and headless-test code | Internal unless a public use case appears |

Other M8/M7 renderers remain internal until an end-to-end public example needs
them. The existing working slice takes precedence over speculative breadth.

## Decision

1. **Header boundary.** Public code lives under `include/starforge/` and is
   consumed through the `starforge` CMake target. Public headers are
   self-contained and may include the C++ standard library and other public
   STARFORGE headers, but must not include or name Vulkan, VMA, SDL, GLM,
   shader, or internal `src/` types.

2. **Opaque resource ownership.** Public backend owners are move-only,
   destructor-managed C++ objects. They hide platform and Vulkan handles in
   private implementation types and destroy dependent resources in reverse
   order. Move assignment first retires the existing resource after the
   caller has made GPU work idle through the public lifetime API.

3. **Application boundary.** `starforge::Application` owns window and event
   state. It does not expose a surface, instance, device, queue, or swapchain.
   Renderer construction and frame submission are separate public owners so
   application setup can be tested without requiring a swapchain.

4. **Scene parameters, not backend setup.** Callers pass application size,
   camera/math data, color, time, radii, and scene-level configuration. The
   backend chooses attachment formats, shader artifacts, pipelines, images,
   buffers, and synchronization primitives.

5. **Error policy.** Public factories and fallible operations return
   `std::expected<T, std::string>`. `std::string` remains the initial
   diagnostic representation to keep the M9 surface independent of logging
   implementation details. Backend errors are translated at the facade
   boundary rather than returned as Vulkan result codes.

6. **Dependency direction.** The implementation may depend on internal
   libraries, but no public header may include an internal header. The
   sandbox migration (9.7) proves this by compiling with only the public
   include directory and the public CMake target.

7. **Stability before 1.0.** Names and parameters in M9 are shaped from the
   working slice but carry no external compatibility guarantee until an
   explicit 1.0 stability decision is recorded.

## Alternatives considered

- **Alias existing internal headers:** rejected because `platform::Window`
  includes `<vulkan/vulkan.h>` and exposes a Vulkan surface factory.
- **Expose backend objects as public wrappers around `Vk*` handles:**
  rejected because it makes every caller depend on Vulkan synchronization and
  lifetime rules.
- **Expose the full internal effect catalog immediately:** rejected under
  ADR-0003; unused effects would freeze unproven interfaces.

## Consequences

- Public users can depend on stable scene-level concepts without learning the
  Vulkan backend.
- Some internal convenience is intentionally unavailable in M9; it can be
  added only when a public example demonstrates the need.
- The leakage gate in 9.8 is part of the API contract, not just code style.
