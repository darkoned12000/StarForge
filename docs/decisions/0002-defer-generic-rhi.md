# ADR-0002: Defer a generic backend abstraction

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

STARFORGE currently has one planned backend: Vulkan. An abstraction designed
for hypothetical backends risks encoding assumptions before a second backend
or a real renderer provides evidence about what should be shared.

## Decision

Do not introduce a generic rendering hardware interface (RHI) until a second
backend is an actual project requirement. Keep Vulkan implementation details
private from the public API, but allow internal code to use Vulkan directly.

## Consequences

- Avoid speculative backend abstractions and their maintenance cost.
- Keep subsystem boundaries and public interfaces deliberate so a future
  second backend remains possible.
- Revisit this decision when a second backend is approved, not merely when one
  is discussed.
