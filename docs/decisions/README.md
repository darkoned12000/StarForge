# Architectural decision records

This directory records decisions that constrain implementation, including
their status and consequences. An accepted record describes a choice already
established by the project documents; it is not a claim that the related code
is already implemented. Open questions remain open until a record is accepted.

## Accepted

| ID | Decision | Status |
|----|----------|--------|
| [0001](0001-vulkan-baseline.md) | Vulkan backend and version floor | Accepted |
| [0002](0002-defer-generic-rhi.md) | Defer a generic backend abstraction | Accepted |
| [0003](0003-public-api-after-vertical-slice.md) | Shape the public API after a working slice | Accepted |
| [0004](0004-vma-allocator.md) | Use VMA for GPU memory allocation | Accepted |
| [0005](0005-math-library.md) | Math library choice (GLM wrapped behind STARFORGE aliases) | Accepted |

## Open decisions

These are deliberately not settled by this index. Evaluate them at the
milestone named below and record the alternatives, evidence, and decision in a
numbered ADR before dependent implementation begins.

| Topic | Target milestone | Existing constraint |
|-------|-----------------|---------------------|
| World-position representation | M2.3 | Prove local precision at astronomical origins; account for the M6 scale hierarchy. |
| Tracy adoption | M4.4 | Decide using project profiling needs; do not add it speculatively. |

## Related notes

- [001-world-scale-requirements.md](001-world-scale-requirements.md) — World scale requirements and acceptance criteria for M2 (agreed).

## Record format

Each `NNNN-title.md` should state **Status**, **Context**, **Decision**, and
**Consequences**. Include alternatives and measurable evidence where relevant.
If new evidence changes an accepted decision, supersede it with a new record
rather than silently rewriting project history.
