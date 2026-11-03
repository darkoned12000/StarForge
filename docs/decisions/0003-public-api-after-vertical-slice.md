# ADR-0003: Shape the public API after a working vertical slice

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

The library's value is in space-focused rendering workflows, but those
workflows and their useful abstractions have not yet been proven by an end-to-
end implementation. Designing a broad public API first would risk freezing
unvalidated concepts.

## Decision

Build and exercise a working internal rendering slice before committing to the
developer-facing API. Keep `include/starforge/` intentionally minimal until
that slice demonstrates the concepts the API needs to expose.

## Consequences

- The sandbox can use internal interfaces while the design is still changing.
- Once the slice works, rebuild its example through public headers and ensure
  no Vulkan types leak through those interfaces.
- Public API work is planned for M9, after the rendering and domain concepts
  have evidence behind them.
