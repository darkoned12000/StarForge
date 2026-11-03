# ADR-0001: Vulkan backend and version floor

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

Vulkan is the first GPU backend. The project needs a stable minimum feature
set while allowing newer implementations to be used when available. Vulkan
1.3 provides the core dynamic-rendering and synchronization2 functionality the
planned renderer uses; buffer device address is also a hard device feature
requirement.

## Decision

Use Vulkan as the initial private backend. Require Vulkan 1.3 and request
Vulkan 1.4 when the loader supports it. Require dynamic rendering,
synchronization2, buffer device address, and a graphics queue. Do not expose
Vulkan types from developer-facing headers.

## Consequences

- Devices that do not meet the hard requirements are rejected.
- Vulkan 1.4-only functionality must not become an implicit requirement.
- Optional features remain capability-gated and require a fallback before
  dependent systems use them.
- The public API remains backend-independent even though the implementation
  is Vulkan-specific.
