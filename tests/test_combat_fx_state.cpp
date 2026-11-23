// M17.5 T1 proof: fixed-capacity pools, graceful exhaustion, deterministic
// output and eventual retirement of every transient effect.
#include <doctest/doctest.h>

#include "combat_fx_state.hpp"

#include <array>
#include <cstdint>
#include <vector>

using namespace starforge;
using namespace starforge::game;

namespace
{
std::vector<CombatFxState> makeStates(std::uint64_t seed)
{
    std::vector<CombatFxState> states;
    states.emplace_back(seed);
    states.emplace_back(seed);
    return states;
}
} // namespace

TEST_CASE("M17.5: effect pools clamp instead of growing")
{
    CombatFxState fx(42);
    const math::Vec3f origin{0.0f, 0.0f, -10.0f};
    const math::Vec3f dir{0.0f, 0.0f, -1.0f};
    const math::Vec4f color{1.0f, 0.6f, 0.3f, 2.0f};
    for (int i = 0; i < 1000; ++i)
        fx.onWeaponFired(origin, dir, color, 0.3f, 3.0f);
    CHECK(fx.boltCount() <= CombatFxState::kMaxBolts);

    // Every particle spawn comes from one of the capped pools.
    for (int i = 0; i < 1000; ++i)
        fx.onImpact(origin, color, true);
    CHECK(fx.particleCount() <= CombatFxState::kMaxParticles);
    CHECK(fx.explosionCount() <= CombatFxState::kMaxExplosions);

    std::array<math::Vec3f, 4> debris{};
    for (const auto& p : debris) { (void)p; }
    for (int i = 0; i < 100; ++i)
        fx.onShipDestroyed(origin, 6.0f, color, debris, true);
    CHECK(fx.explosionCount() <= CombatFxState::kMaxExplosions);
    CHECK(fx.instances().size() <= CombatFxState::kMaxInstances);

    // Repeated updates must not accumulate instances.
    for (int i = 0; i < 60; ++i) fx.update(1.0f / 60.0f);
    CHECK(fx.instances().size() <= CombatFxState::kMaxInstances);
}

TEST_CASE("M17.5: effects are deterministic for the same seed")
{
    auto states = makeStates(0xABCDEFu);
    const math::Vec3f origin{0.0f, 0.0f, -10.0f};
    const math::Vec3f dir{0.0f, 0.0f, -1.0f};
    const math::Vec4f color{1.0f, 0.5f, 0.2f, 2.0f};
    std::array<math::Vec3f, 3> debris{math::Vec3f{1.0f, 0.0f, 0.0f},
                                      math::Vec3f{0.0f, 1.0f, 0.0f},
                                      math::Vec3f{0.0f, 0.0f, 1.0f}};
    for (auto& fx : states)
    {
        fx.onWeaponFired(origin, dir, color, 0.3f, 3.0f);
        fx.onImpact(origin, color, false);
        fx.onShipDestroyed(origin, 6.0f, color, debris, true);
        fx.setEngines({-0.4f, 0.0f, 2.0f}, {0.4f, 0.0f, 2.0f},
                      {0.0f, 0.0f, 1.0f}, color, 0.6f, false, 1.25f);
        for (int i = 0; i < 8; ++i) fx.update(1.0f / 60.0f);
    }
    const auto& a = states[0].instances();
    const auto& b = states[1].instances();
    REQUIRE(a.size() == b.size());
    CHECK(a.size() > 0);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK(a[i].position.x() == doctest::Approx(b[i].position.x()));
        CHECK(a[i].position.y() == doctest::Approx(b[i].position.y()));
        CHECK(a[i].position.z() == doctest::Approx(b[i].position.z()));
        CHECK(a[i].color.w() == doctest::Approx(b[i].color.w()));
        CHECK(a[i].kind == b[i].kind);
    }
}

TEST_CASE("M17.5: all transients retire and camera shake decays")
{
    CombatFxState fx(7);
    const math::Vec3f origin{0.0f, 0.0f, -10.0f};
    const math::Vec3f dir{0.0f, 0.0f, -1.0f};
    const math::Vec4f color{1.0f, 0.5f, 0.2f, 2.0f};
    fx.onWeaponFired(origin, dir, color, 0.3f, 3.0f);
    fx.onShieldHit(origin, color, 2.0f);
    std::array<math::Vec3f, 2> debris{origin, origin};
    fx.onShipDestroyed(origin, 6.0f, color, debris, false);
    fx.setEngines(origin, origin, {0.0f, 0.0f, 1.0f}, color, 1.0f, true, 0.0f);
    for (int i = 0; i < 10; ++i) fx.update(1.0f / 60.0f);
    CHECK(fx.boltCount() > 0);
    const auto initialShake = fx.cameraShake({0.0f, 0.0f, 8.0f});

    for (int i = 0; i < 200; ++i) fx.update(1.0f / 60.0f);
    CHECK(fx.boltCount() == 0);
    CHECK(fx.particleCount() == 0);
    CHECK(fx.explosionCount() == 0);
    const auto finalShake = fx.cameraShake({0.0f, 0.0f, 8.0f});
    CHECK(finalShake.x() == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(finalShake.y() == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(finalShake.z() == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(std::abs(finalShake.x()) <= std::abs(initialShake.x()) + 1e-6f);

    fx.clear();
    CHECK(fx.instances().empty());
}
