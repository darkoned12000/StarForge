#pragma once

#include "ai/components.hpp"
#include "ecs/world.hpp"
#include "physics/physics_world.hpp"
#include "spatial/world_position.hpp"

#include <cstdint>

namespace starforge::ai
{

// Combat component/result declarations. Their data components live in
// ai/components.hpp; the simulation helpers are implemented in combat.cpp.

struct CombatShotResult
{
    bool fired = false;
    float damage = 0.0f;
};

// Advances cooldown timers. Call once per fixed tick before weapons are
// allowed to fire again.
void tickWeapons(ecs::Ecs& world, double dt) noexcept;

// Removes transient DamageEvent/CombatFireEvent components. These are
// single-tick event payloads: call once at frame end so consumers never
// observe stale hits on later frames.
void clearCombatEvents(ecs::Ecs& world) noexcept;

// Returns true when a camera-relative ray from the attacker's hull centre
// reaches the target body. Uses the Jolt world's raycast with the
// attacker's own body filtered out; the hit body is matched by Flecs user
// data so shots stop at other bodies (blockers) and the target itself.
[[nodiscard]] bool hasLineOfSight(physics::PhysicsWorld& physics, const spatial::WorldPosition& camera,
                                  flecs::entity attacker, flecs::entity target);

// Single range/LOS/cooldown check followed by a direct damage event.
// The caller supplies camera in world coordinates; all geometry stays
// camera-relative after the initial conversion.
[[nodiscard]] CombatShotResult tryFire(ecs::Ecs& world, physics::PhysicsWorld& physics, flecs::entity attacker,
                                       flecs::entity target, const spatial::WorldPosition& camera);

} // namespace starforge::ai
