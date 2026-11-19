#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace starforge::gameplay
{

enum class Subsystem : std::size_t
{
    Hull = 0,
    Engine,
    Weapons,
    Shields,
    Count,
};

struct DamageState
{
    float shieldMax = 100.0f;
    float shield = 100.0f;
    float hullMax = 100.0f;
    float hull = 100.0f;
    std::array<float, static_cast<std::size_t>(Subsystem::Count)> subsystemHealth{
        100.0f, 100.0f, 100.0f, 100.0f};
    bool wreck = false;
};

struct DamageResult
{
    float shieldAbsorbed = 0.0f;
    float hullDamage = 0.0f;
    bool subsystemHit = false;
    Subsystem subsystem = Subsystem::Hull;
    bool destroyed = false;
    bool shieldBroke = false;
};

// Applies damage shield-first, then hull. A subsystem can be hit directly;
// its health degrades in parallel. Zero hull marks the entity as a wreck.
[[nodiscard]] DamageResult applyDamage(DamageState& state, float amount,
                                       Subsystem subsystem = Subsystem::Hull,
                                       float shieldPierce = 0.0f);

// Recharges shields over time. A destroyed shield subsystem cannot recharge.
void tickDamage(DamageState& state, float dt, float shieldRechargePerSecond);

// Derived multipliers used by flight/weapon systems.
[[nodiscard]] float engineEfficiency(const DamageState& state) noexcept;
[[nodiscard]] float weaponEfficiency(const DamageState& state) noexcept;
[[nodiscard]] float shieldEfficiency(const DamageState& state) noexcept;

} // namespace starforge::gameplay
