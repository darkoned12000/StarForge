#include "ai/combat.hpp"

#include "ai/perception.hpp"
#include "ecs/components.hpp"
#include "physics/bridge.hpp"
#include "spatial/camera_relative.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace starforge::ai
{

void tickWeapons(ecs::Ecs& world, double dt) noexcept
{
    if (dt <= 0.0)
    {
        return;
    }
    world.world().query<Weapon>().each([dt](flecs::entity, Weapon& weapon) {
        if (weapon.timer > 0.0f)
        {
            weapon.timer = std::max(0.0f, weapon.timer - static_cast<float>(dt));
        }
    });
}

void clearCombatEvents(ecs::Ecs& world) noexcept
{
    // Structural removal inside a query callback is illegal in Flecs
    // (locked table), so collect first, then remove outside iteration.
    // Scratch storage is reused across calls (capacity retained) so the
    // per-frame cleanup does not allocate.
    static thread_local std::vector<flecs::entity> damagedStorage;
    auto* damaged = &damagedStorage;
    damaged->clear();
    world.world().query<DamageEvent>().each(
        [damaged](flecs::entity entity, DamageEvent&) { damaged->push_back(entity); });
    for (const auto entity : *damaged)
    {
        entity.remove<DamageEvent>();
    }

    static thread_local std::vector<flecs::entity> firedStorage;
    auto* fired = &firedStorage;
    fired->clear();
    world.world().query<CombatFireEvent>().each(
        [fired](flecs::entity entity, CombatFireEvent&) { fired->push_back(entity); });
    for (const auto entity : *fired)
    {
        entity.remove<CombatFireEvent>();
    }
}

bool hasLineOfSight(physics::PhysicsWorld& physics, const spatial::WorldPosition& camera,
                    flecs::entity attacker, flecs::entity target)
{
    if (!attacker.has<ecs::Transform>() || !target.has<ecs::Transform>())
    {
        return false;
    }

    const auto& from = attacker.get<ecs::Transform>().position;
    const auto& to = target.get<ecs::Transform>().position;
    const math::Vec3d origin = spatial::toCameraRelative(from, camera);
    const math::Vec3d displacement = spatial::toCameraRelative(to, from);
    const float distance = static_cast<float>(glm::length(displacement));
    if (distance <= 1.0e-6f)
    {
        return true;
    }

    const JPH::Vec3 fromFloat{static_cast<float>(origin.x), static_cast<float>(origin.y),
                              static_cast<float>(origin.z)};
    const JPH::Vec3 directionVec{static_cast<float>(displacement.x), static_cast<float>(displacement.y),
                                 static_cast<float>(displacement.z)};
    // The ray originates at the hull centre, which is inside the
    // attacker's own collider for any ship larger than a point, so the
    // attacker's body is filtered out of the raycast instead of the origin
    // being nudged forward by a fixed offset.
    const physics::BodyHandle ignoreBody =
        attacker.has<physics::PhysicsBody>() ? attacker.get<physics::PhysicsBody>().body : physics::BodyHandle{};
    const auto hit = physics.castRay(fromFloat, directionVec, physics::PhysicsWorld::RaycastTarget::SolidOnly,
                                     ignoreBody);
    if (!hit.has_value())
    {
        return false;
    }

    const std::uint64_t hitUserData = physics.system().GetBodyInterface().GetUserData(hit->body.id);
    return hitUserData == target.id();
}

CombatShotResult tryFire([[maybe_unused]] ecs::Ecs& world, physics::PhysicsWorld& physics, flecs::entity attacker,
                         flecs::entity target, const spatial::WorldPosition& camera)
{
    CombatShotResult result;
    if (!attacker.is_alive() || !target.is_alive() || attacker == target ||
        !attacker.has<ecs::Transform>() || !target.has<ecs::Transform>() ||
        !attacker.has<Weapon>() || !target.has<Health>() || !attacker.has<Health>() ||
        !attacker.has<Faction>() || !target.has<Faction>())
    {
        return result;
    }

    // Dead ships are wrecks: never fire at (or from) a 0-HP entity.
    if (target.get<Health>().currentHealth <= 0.0f || attacker.get<Health>().currentHealth <= 0.0f)
    {
        return result;
    }

    // Read-only checks first: get_mut below would mark Weapon modified
    // (firing change observers) even when no shot is fired.
    const auto& weapon = attacker.get<Weapon>();
    if (weapon.timer > 0.0f || weapon.range <= 0.0f)
    {
        return result;
    }

    const std::uint32_t selfFaction = attacker.get<Faction>().id;
    const std::uint32_t targetFaction = target.get<Faction>().id;
    if (classifyFaction(selfFaction, targetFaction) != FactionRelation::Hostile)
    {
        return result;
    }

    const math::Vec3d relative =
        spatial::toCameraRelative(target.get<ecs::Transform>().position, attacker.get<ecs::Transform>().position);
    const double distanceSquared = glm::dot(relative, relative);
    if (distanceSquared > static_cast<double>(weapon.range) * static_cast<double>(weapon.range))
    {
        return result;
    }

    if (!hasLineOfSight(physics, camera, attacker, target))
    {
        return result;
    }

    // All gates passed: apply effects.
    const float damage = weapon.damage;
    auto& weaponMut = attacker.get_mut<Weapon>();
    weaponMut.timer = weaponMut.cooldown;

    auto& healthMut = target.get_mut<Health>();
    healthMut.currentHealth = std::max(0.0f, healthMut.currentHealth - damage);
    target.modified<Health>();

    // Accumulate same-tick hits so multiple attackers never clobber one
    // another's DamageEvent. clearCombatEvents() resets this at frame end.
    if (target.has<DamageEvent>())
    {
        auto& event = target.get_mut<DamageEvent>();
        event.damage += damage;
        target.modified<DamageEvent>();
    }
    else
    {
        target.set<DamageEvent>({attacker, damage});
    }
    attacker.set<CombatFireEvent>({damage});

    result.fired = true;
    result.damage = damage;
    return result;
}

} // namespace starforge::ai
