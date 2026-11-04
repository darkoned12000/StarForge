# STARFORGE Vision

## Mission

Create a specialized rendering library designed specifically
for 3D space games.

## What STARFORGE Is

STARFORGE is a **rendering library** that sits on top of Vulkan.
It is not a general-purpose graphics API in the sense of Vulkan,
Metal or wgpu, and it does not try to wrap every GPU concept.

Its value is in what generic engines do poorly: precision across
astronomical distances, GPU-driven management of enormous object
counts, and scalable planetary rendering. Basic Vulkan setup is a
means to that end, not the product.

## Primary Design Goals

1. High-performance GPU rendering.
2. Efficient rendering of enormous object counts.
3. Accurate positioning across astronomical distances.
4. Scalable planetary rendering.
5. Advanced space environments.
6. GPU-driven scene management.
7. Simple developer-facing API.

## Architecture Principles

- Vulkan is initially the private GPU backend.
- Public interfaces must not expose Vulkan or VMA types.
- Rendering systems must be modular.
- GPU resource management must be centralized.
- Every GPU subsystem is verifiable headlessly without requiring a display window.
- Performance must be measured.
- Hardware-specific features must have fallbacks.
- Cross-platform support must remain possible.
- No generic backend abstraction (RHI) until a second backend
  exists. Speculative abstractions are usually wrong.
- The public API is designed from a working vertical slice,
  not before it.

## Non-Goals (for now)

- A general-purpose graphics API.
- A second GPU backend.
- A full game engine (scene editor, audio, physics, scripting).

STARFORGE itself remains a rendering library with the original non-goals:
no physics, audio, full engine, or scripting. The long-term goal is to
build games (Everspace 2 / Freelancer / Limit Theory style) on top of
STARFORGE. Game systems will be developed as a separate layer consuming
STARFORGE's public API once M9 is complete. See the Game Layer section
in ROADMAP.md for the full milestone plan and recommended tech stack
(Jolt Physics, miniaudio, Flecs/EnTT, Dear ImGui, FastNoise2, etc.).

## Initial Platform

Linux / EndeavourOS.

## Initial GPU

AMD Radeon RX 7900 XTX.

## Initial Graphics Backend

Vulkan 1.3 is the minimum (dynamic rendering, synchronization2,
buffer device address). Vulkan 1.4 is requested when available.

## Initial Language

C++26.

## Shader Language

Slang.

## Ownership and Licensing

STARFORGE is a proprietary project. All rights are reserved by the author;
see [LICENSE](../LICENSE).
