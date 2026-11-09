# ADR-0012: Deterministic content vs cosmetic effect noise

- **Status:** Accepted (M13 refinement)
- **Supersedes:** The noise-scope wording of ADR-0011.
- **Context:** ADR-0011 prohibits architecture-specific SIMD noise in deterministic
  generation paths. That constraint was aimed at authoritative world content, not
  every possible visual effect in the engine.
- **Decision:**
  - Authoritative procedural content — the objects, positions, scales, subtypes,
    and any geometry derived from them — stays integer-hash based and bit-exact.
    It does not use FastNoise2 or any floating-point noise routine whose result
    can vary by architecture.
  - Purely cosmetic shader/runtime effects (animated nebula turbulence, smoke,
    dust, impact particles, other non-authoritative decoration) may use
    conventional floating-point noise or hash functions. Their output is not part
    of the generated world state and does not need cross-machine bit equality.
  - Effect systems that feed gameplay, collision, networking, or persistent state
    are authoritative and fall back to the deterministic rule.
- **Consequences:**
  - The generator remains machine-independent; renderers retain freedom to make
    attractive effects without changing world determinism.
  - Reviewers should reject floating-point noise in `src/content/` but should not
    reject it in cosmetic effect shaders.
