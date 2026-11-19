#include "gameplay/damage.hpp"

#include <algorithm>

namespace starforge::gameplay
{

DamageResult applyDamage(DamageState& state, float amount, Subsystem subsystem, float shieldPierce)
{
    DamageResult result;
    if (state.wreck || amount <= 0.0f) return result;

    result.subsystem = subsystem;
    const std::size_t index = static_cast<std::size_t>(subsystem);
    if (index < state.subsystemHealth.size())
    {
        state.subsystemHealth[index] = std::max(0.0f, state.subsystemHealth[index] - amount * 0.5f);
        result.subsystemHit = true;
    }

    float remaining = amount;
    const float pierced = std::clamp(shieldPierce, 0.0f, 1.0f) * amount;
    const float shieldable = remaining - pierced;
    const float absorbed = std::min(state.shield, std::max(0.0f, shieldable));
    state.shield -= absorbed;
    result.shieldAbsorbed = absorbed;
    remaining -= absorbed; // includes the shield-piercing portion

    if (state.shield <= 0.0f && absorbed > 0.0f) result.shieldBroke = true;
    if (remaining > 0.0f)
    {
        const float hullDamage = std::min(state.hull, remaining);
        state.hull -= hullDamage;
        result.hullDamage = hullDamage;
        if (state.hull <= 0.0f)
        {
            state.hull = 0.0f;
            state.wreck = true;
            result.destroyed = true;
        }
    }
    return result;
}

void tickDamage(DamageState& state, float dt, float shieldRechargePerSecond)
{
    if (state.wreck) return;
    const float efficiency = shieldEfficiency(state);
    if (efficiency > 0.0f && state.shield < state.shieldMax)
    {
        state.shield = std::min(state.shieldMax,
                                state.shield + shieldRechargePerSecond * efficiency * dt);
    }
}

float engineEfficiency(const DamageState& state) noexcept
{
    return std::clamp(state.subsystemHealth[static_cast<std::size_t>(Subsystem::Engine)] / 100.0f, 0.0f, 1.0f);
}
float weaponEfficiency(const DamageState& state) noexcept
{
    return std::clamp(state.subsystemHealth[static_cast<std::size_t>(Subsystem::Weapons)] / 100.0f, 0.0f, 1.0f);
}
float shieldEfficiency(const DamageState& state) noexcept
{
    return std::clamp(state.subsystemHealth[static_cast<std::size_t>(Subsystem::Shields)] / 100.0f, 0.0f, 1.0f);
}

} // namespace starforge::gameplay
