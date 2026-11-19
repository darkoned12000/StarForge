// M15.0.5 / 15.1-15.5 exit proof (T1 + T5). Drives the exact GameSimulation
// used by apps/game headlessly: combat, damage, docking, trading loop, missions,
// progression, authored overrides, jumping and a 5-minute CPU budget gate.
#include <doctest/doctest.h>

#include "game_simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

using namespace starforge::game;

namespace
{
constexpr float kStep = 1.0f / 60.0f;
const RawVec3f kForward{0.0f, 0.0f, -1.0f};
} // namespace

TEST_CASE("M15.0.5: hostile engages, player fires, shield/hull damage and destruction")
{
    GameSimulation sim;
    sim.spawnHostile();
    REQUIRE(sim.hostileAlive());

    // Player fires primary at the hostile in front; shield breaks then hull.
    bool sawHostileHit = false;
    bool sawDestroyed = false;
    for (int i = 0; i < 600 && sim.hostileAlive(); ++i)
    {
        auto events = sim.step(kStep, kForward, true, false);
        for (const auto& event : events)
        {
            if (event.type == GameEvent::Type::HostileHit) sawHostileHit = true;
            if (event.type == GameEvent::Type::HostileDestroyed) sawDestroyed = true;
        }
    }
    CHECK(sawHostileHit);
    CHECK(sawDestroyed);
    CHECK_FALSE(sim.hostileAlive());

    // A hostile that is not shot deals shield damage to the player.
    GameSimulation sim2;
    sim2.spawnHostile();
    bool sawShieldHit = false;
    for (int i = 0; i < 180; ++i)
    {
        auto events = sim2.step(kStep, kForward, false, false);
        for (const auto& event : events)
            if (event.type == GameEvent::Type::PlayerShieldHit) sawShieldHit = true;
    }
    CHECK(sawShieldHit);
    CHECK(sim2.playerShield() < 100.0f);
}

TEST_CASE("M15.0.5: hostile breaks off and evades when hit")
{
    GameSimulation sim;
    sim.spawnHostile();
    // Fire until the first hit registers.
    for (int i = 0; i < 120 && sim.hostile().engaging; ++i)
        (void)sim.step(kStep, kForward, true, false);
    CHECK_FALSE(sim.hostile().engaging); // reactive evade on damage
}

TEST_CASE("M15.1: player damage wrecks and repair restores")
{
    GameSimulation sim;
    // The hostile eventually depletes shields and hull if left unchecked.
    sim.spawnHostile();
    for (int i = 0; i < 3000 && !sim.playerWrecked(); ++i)
        (void)sim.step(kStep, kForward, false, false);
    CHECK(sim.playerWrecked());
    CHECK(sim.playerHull() == 0.0f);
    sim.repairPlayer();
    CHECK_FALSE(sim.playerWrecked());
    CHECK(sim.playerHull() == 100.0f);
}

TEST_CASE("M15.2/15.5: trading loop - dock, buy, fly, sell, repeat")
{
    GameSimulation sim;
    sim.configureMarket("station-a", "ore", 20, 40);
    const int startingCredits = sim.credits();

    sim.updateDocking(20.0, 0.0, "station-a");
    REQUIRE(sim.dock("station-a", "slot-1"));
    CHECK(sim.isDocked());

    REQUIRE(sim.buy("station-a", "ore", 5));
    CHECK(sim.cargoCount("ore") == 5);
    CHECK(sim.credits() == startingCredits - 100);
    CHECK(sim.stock("station-a", "ore") == 35);

    // "Fly" (dock state is separate from position; undock simulates departure).
    sim.undock();
    CHECK_FALSE(sim.isDocked());
    sim.updateDocking(20.0, 0.0, "station-a");
    REQUIRE(sim.dock("station-a", "slot-1"));

    REQUIRE(sim.sell("station-a", "ore", 5));
    CHECK(sim.cargoCount("ore") == 0);
    CHECK(sim.credits() == startingCredits);
    CHECK(sim.stock("station-a", "ore") == 40);

    // Second loop with a different quantity proves repeatability.
    REQUIRE(sim.buy("station-a", "ore", 3));
    REQUIRE(sim.sell("station-a", "ore", 3));
    CHECK(sim.credits() == startingCredits);
}

TEST_CASE("M15.3/15.4: mission reward unlocks jump range and jump crosses systems")
{
    GameSimulation sim;
    CHECK(sim.jumpRangeLy() == doctest::Approx(1.0f));
    CHECK_FALSE(sim.jump("alpha-centauri", 1.5f)); // beyond base range

    REQUIRE(sim.acceptMission("haul-ore"));
    REQUIRE(sim.buy("station-a", "ore", 10));
    REQUIRE(sim.deliverMission("haul-ore", "ore", 10, "station-b"));
    REQUIRE(sim.completeMission("haul-ore", "station-b"));

    CHECK(sim.completedMissions() == 1);
    CHECK(sim.jumpRangeLy() == doctest::Approx(2.0f)); // unlock gate opened
    CHECK(sim.credits() == 1300);                      // 1000 - 200 (10 ore) + 500 reward
    REQUIRE(sim.jump("alpha-centauri", 1.5f));
    CHECK(sim.currentSystem() == "alpha-centauri");
}

TEST_CASE("M15.4: authored placement wins and player mods survive regeneration")
{
    GameSimulation sim;
    starforge::gameplay::CellKey cell{1, 2, 3};
    starforge::gameplay::ContentPlacement generated;
    generated.id = "gen-rock";
    generated.cell = cell;
    generated.kind = starforge::gameplay::ContentKind::Asteroid;
    starforge::gameplay::ContentPlacement authored;
    authored.id = "story-station";
    authored.cell = cell;
    authored.kind = starforge::gameplay::ContentKind::Station;
    sim.addAuthored(authored);

    CHECK(sim.resolveContent(cell, {generated}) == 2); // authored added
    sim.setPlayerMod("story-station", starforge::gameplay::PlayerModType::Destroyed);
    CHECK(sim.resolveContent(cell, {generated}) == 1); // generated remains, authored destroyed
}

TEST_CASE("M15.0.6: five-minute flight simulation stays under the frame budget (T5)")
{
    GameSimulation sim;
    sim.spawnHostile();
    std::vector<double> timings;
    timings.reserve(6000);
    const int totalSteps = 5 * 60 * 60; // 5 minutes at 60 Hz

    for (int i = 0; i < totalSteps; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        (void)sim.step(kStep, kForward, (i % 20) < 3, (i % 120) == 0);
        const auto end = std::chrono::steady_clock::now();
        if (i >= 600) // discard warm-up
            timings.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(timings.begin(), timings.end());
    const double p95 = timings[(timings.size() * 95) / 100];
    CHECK(p95 < 6.94); // ADR-004 interval; gameplay must leave room for rendering
}

TEST_CASE("M16 polish: spawnNpcs creates traveling hostiles and neutrals")
{
    GameSimulation sim;
    sim.spawnNpcs(5);
    REQUIRE(sim.npcs().size() == 5);

    int hostiles = 0, neutrals = 0;
    for (const auto& npc : sim.npcs())
    {
        CHECK(npc.alive);
        CHECK_FALSE(npc.id.empty());
        if (npc.hostile) ++hostiles; else ++neutrals;
    }
    CHECK(hostiles >= 1);
    CHECK(neutrals >= 1); // not every NPC attacks the player
    CHECK(sim.hostileAlive());

    // Every NPC keeps moving through the sector over time.
    const auto before = sim.npcs();
    for (int i = 0; i < 120; ++i)
        (void)sim.step(kStep, kForward, false, false);
    const auto after = sim.npcs();
    std::size_t moved = 0;
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        const auto& a = before[i].position;
        const auto& b = after[i].position;
        if (std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) > 1.0f)
            ++moved;
    }
    CHECK(moved == before.size());
}

TEST_CASE("M16 polish: weapons can be aimed at a selected neutral NPC")
{
    GameSimulation sim;
    sim.spawnNpcs(5);

    const auto neutral = std::find_if(sim.npcs().begin(), sim.npcs().end(),
        [](const NpcInfo& n) { return !n.hostile; });
    REQUIRE(neutral != sim.npcs().end());
    sim.setTargetId(neutral->id);

    bool hitNeutral = false;
    for (int i = 0; i < 600; ++i)
        for (const auto& event : sim.step(kStep, kForward, true, false))
            if (event.type == GameEvent::Type::HostileHit && event.sourceId == neutral->id)
                hitNeutral = true;
    CHECK(hitNeutral);

    // The selected neutral can be destroyed just like a hostile.
    const auto now = std::find_if(sim.npcs().begin(), sim.npcs().end(),
        [&](const NpcInfo& n) { return n.id == neutral->id; });
    REQUIRE(now != sim.npcs().end());
    CHECK_FALSE(now->alive);
}
