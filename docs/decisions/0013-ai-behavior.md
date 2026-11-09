# ADR-0013: AI and behavior architecture

- **Status:** Accepted (M14 Phase A)
- **Context:** M14 adds ship behavior on top of the deterministic ECS and
  physics layers. The engine reach and camera-relative transforms mean AI
  must reason in local coordinates, never in astronomical absolute floats.
- **Decision:**
  - **Internal game layer:** AI lives in `src/ai` (`starforge_ai`) and is
    not promoted to the public facade until a public consumer justifies it
    (ADR-0009).
  - **Components:** `AiAgent` (state, target, think timers, perception and
    steering profile), `Faction` (membership), and `NavigationTarget`
    (destination and arrival radius) are plain data components on ECS
    entities.
  - **Fixed-tick determinism:** All simulation updates use fixed-step
    deltas. AI decisions never read wall-clock time and never use physics
    callbacks directly.
  - **Perception:** target pickup uses the existing `SpatialIndex` in a
    3×3×3 neighbourhood; local flight in Phase A is far smaller than one
    2²⁰ m cell, so the world-cell neighbourhood is a sound acceleration
    structure for the Phase A perception radius. Later phases may add a
    finer local grid for radii larger than one world cell.
  - **Threading:** AI runs on the main/Flecs thread. Worker or audio
    threads never mutate ECS data; Jolt callbacks stay in M11's deferred
    queue.
  - **Physics coupling:** Phase B drives kinematic NPC bodies through
    M11's existing ECS→Jolt bridge; Dynamic bodies keep their Jolt-owned
    velocity integration.
- **Alternatives considered:**
  - **Behavior trees (deferred):** More flexible, but heavier than the
    finite-state profiles needed for patrol/transit/combat until Phase B.
  - **A* pathfinding on a nav mesh (deferred):** Open-space navigation can
    use steering around local obstacles in Phase B; a nav mesh adds
    authoring and stability requirements that do not pay off yet.
- **Consequences:**
  - `AiAgent` is stable for the rest of M14.
  - Perception is O(n) in the Phase A flight bubble; hopping to 50 agents is
    cheap.
  - Behavior states exist in the component layout now, while the FSM that
    drives them lands in M14.4.
