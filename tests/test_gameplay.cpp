// M15 gameplay systems (T1, CPU only): damage/shields/subsystems, cargo/economy,
// missions/progression, authored overrides, jumping and docking.
#include <doctest/doctest.h>

#include "gameplay/authored.hpp"
#include "gameplay/damage.hpp"
#include "gameplay/docking.hpp"
#include "gameplay/economy.hpp"
#include "gameplay/jumping.hpp"
#include "gameplay/missions.hpp"
#include "gameplay/progression.hpp"

#include <cmath>

using namespace starforge::gameplay;

// ---------------------------------------------------------------------------
// 15.1 Damage / shields / subsystems / destruction
// ---------------------------------------------------------------------------

TEST_CASE("M15 damage: shield absorbs before hull")
{
    DamageState state;
    state.shield = 40.0f;
    state.hull = 100.0f;
    auto result = applyDamage(state, 30.0f);
    CHECK(result.shieldAbsorbed == doctest::Approx(30.0f));
    CHECK(result.hullDamage == doctest::Approx(0.0f));
    CHECK(state.shield == doctest::Approx(10.0f));
    CHECK(state.hull == doctest::Approx(100.0f));
    CHECK_FALSE(result.shieldBroke);
    CHECK_FALSE(result.destroyed);
}

TEST_CASE("M15 damage: overflow goes to hull and wrecks at zero")
{
    DamageState state;
    state.shield = 10.0f;
    state.hull = 20.0f;
    auto result = applyDamage(state, 40.0f);
    CHECK(result.shieldAbsorbed == doctest::Approx(10.0f));
    CHECK(result.shieldBroke);
    CHECK(result.hullDamage == doctest::Approx(20.0f));
    CHECK(state.hull == doctest::Approx(0.0f));
    CHECK(state.wreck);
    CHECK(result.destroyed);

    // A wreck takes no further damage.
    auto after = applyDamage(state, 100.0f);
    CHECK(after.hullDamage == doctest::Approx(0.0f));
}

TEST_CASE("M15 damage: shield pierce bypasses shield")
{
    DamageState state;
    state.shield = 100.0f;
    state.hull = 100.0f;
    auto result = applyDamage(state, 100.0f, Subsystem::Hull, 0.5f);
    CHECK(result.shieldAbsorbed == doctest::Approx(50.0f));
    CHECK(result.hullDamage == doctest::Approx(50.0f));
    CHECK(state.shield == doctest::Approx(50.0f));
    CHECK(state.hull == doctest::Approx(50.0f));
}

TEST_CASE("M15 damage: subsystem hits degrade efficiency and gate recharge")
{
    // Large shield pool so repeated subsystem hits do not wreck the hull.
    DamageState state;
    state.shield = state.shieldMax = 1000.0f;
    applyDamage(state, 100.0f, Subsystem::Engine);
    CHECK(engineEfficiency(state) == doctest::Approx(0.5f));
    applyDamage(state, 100.0f, Subsystem::Shields);
    applyDamage(state, 100.0f, Subsystem::Shields); // 2 * (100 * 0.5) = 100 -> destroyed
    CHECK(shieldEfficiency(state) == doctest::Approx(0.0f));
    CHECK_FALSE(state.wreck);

    state.shield = 10.0f;
    tickDamage(state, 1.0f, 5.0f);
    CHECK(state.shield == doctest::Approx(10.0f)); // broken shield subsystem cannot recharge

    DamageState fresh;
    fresh.shield = 10.0f;
    tickDamage(fresh, 1.0f, 5.0f);
    CHECK(fresh.shield == doctest::Approx(15.0f));
}

// ---------------------------------------------------------------------------
// 15.2 Cargo / economy
// ---------------------------------------------------------------------------

TEST_CASE("M15 cargo: capacity and item arithmetic")
{
    CargoHold hold;
    hold.capacity = 10;
    CHECK(hold.canAdd("ore", 10));
    CHECK_FALSE(hold.canAdd("ore", 11));
    hold.add("ore", 6);
    hold.add("water", 2);
    CHECK(hold.used() == 8);
    CHECK(hold.remove("ore", 3));
    CHECK(hold.used() == 5);
    CHECK_FALSE(hold.remove("ore", 99));
    CHECK_FALSE(hold.remove("missing", 1));
}

TEST_CASE("M15 economy: buy and sell update credits, cargo and stock")
{
    EconomyState economy;
    economy.credits = 1000;
    Market market;
    market.stock["ore"] = 10;
    market.setPrice("ore", 20);
    economy.stations["station-a"] = market;

    auto bought = economy.buy("station-a", "ore", 5);
    REQUIRE(bought.has_value());
    CHECK(bought->creditsDelta == -100);
    CHECK(economy.credits == 900);
    CHECK(economy.cargo.items["ore"] == 5);
    CHECK(economy.stations["station-a"].stock["ore"] == 5);

    auto sold = economy.sell("station-a", "ore", 5);
    REQUIRE(sold.has_value());
    CHECK(sold->creditsDelta == 100);
    CHECK(economy.credits == 1000);
    CHECK(economy.cargo.items.find("ore") == economy.cargo.items.end());
    CHECK(economy.stations["station-a"].stock["ore"] == 10);
}

TEST_CASE("M15 economy: trades fail cleanly on bad input")
{
    EconomyState economy;
    economy.credits = 10;
    Market market;
    market.stock["ore"] = 1;
    market.setPrice("ore", 100);
    economy.stations["station-a"] = market;

    CHECK_FALSE(economy.buy("station-a", "ore", 5).has_value()); // too expensive
    CHECK_FALSE(economy.buy("station-a", "ore", 2).has_value()); // not enough stock
    CHECK_FALSE(economy.buy("unknown", "ore", 1).has_value());
    CHECK_FALSE(economy.sell("station-a", "ore", 1).has_value()); // no cargo
    CHECK_FALSE(economy.buy("station-a", "ore", 0).has_value());  // invalid amount
}

// ---------------------------------------------------------------------------
// 15.3 Missions / progression
// ---------------------------------------------------------------------------

TEST_CASE("M15 missions: accept -> deliver -> complete grants reward and unlock")
{
    MissionBoard board;
    MissionDef def;
    def.id = "haul-1";
    def.reward = 500;
    def.unlock = "jump2";
    def.objectiveCommodity = "ore";
    def.objectiveAmount = 10;
    def.dockStation = "station-b";
    board.define(def);

    REQUIRE(board.accept("haul-1").has_value());
    CHECK(board.active().size() == 1);
    CHECK_FALSE(board.deliver("haul-1", "water", 10, "station-b").has_value());
    CHECK_FALSE(board.deliver("haul-1", "ore", 10, "station-a").has_value());
    auto sufficient = board.deliver("haul-1", "ore", 6, "station-b");
    REQUIRE(sufficient.has_value());
    CHECK_FALSE(*sufficient); // 6 of 10
    sufficient = board.deliver("haul-1", "ore", 4, "station-b");
    REQUIRE(sufficient.has_value());
    CHECK(*sufficient);

    auto reward = board.complete("haul-1", "station-b");
    REQUIRE(reward.has_value());
    CHECK(reward->credits == 500);
    CHECK(reward->unlock == "jump2");
    CHECK(board.completed().size() == 1);
    CHECK_FALSE(board.complete("haul-1", "station-b").has_value()); // not active anymore
}

TEST_CASE("M15 progression: unlock raises jump range idempotently")
{
    Progression progression;
    CHECK(progression.jumpRangeLy() == doctest::Approx(1.0f));
    progression.applyUnlock("jump2");
    CHECK(progression.jumpRangeLy() == doctest::Approx(2.0f));
    progression.applyUnlock("jump2");
    CHECK(progression.jumpRangeLy() == doctest::Approx(2.0f)); // no shrink
    progression.applyUnlock("jump5");
    CHECK(progression.jumpRangeLy() == doctest::Approx(5.0f));
    CHECK(progression.has("jump2"));
    CHECK_FALSE(progression.has("unknown"));
}

// ---------------------------------------------------------------------------
// 15.4 Authored overrides + jumping
// ---------------------------------------------------------------------------

TEST_CASE("M15 authored: authored content wins over generated in its cell")
{
    CellKey cell{1, 2, 3};
    ContentPlacement generated;
    generated.id = "gen-rock";
    generated.cell = cell;
    generated.kind = ContentKind::Asteroid;

    ContentPlacement authored;
    authored.id = "authored-station";
    authored.cell = cell;
    authored.kind = ContentKind::Station;
    authored.priority = 10;

    LayeredGeneration layered;
    layered.addAuthored(authored);

    auto resolved = layered.resolve(cell, {generated});
    // Different kinds coexist: asteroid stays, station is added.
    CHECK(resolved.size() == 2);

    // An authored asteroid suppresses a generated asteroid in the same cell.
    ContentPlacement authoredRock;
    authoredRock.id = "authored-rock";
    authoredRock.cell = cell;
    authoredRock.kind = ContentKind::Asteroid;
    layered.addAuthored(authoredRock);
    resolved = layered.resolve(cell, {generated});
    CHECK(resolved.size() == 2); // station + authored-rock, gen-rock suppressed
    bool foundGenerated = false;
    for (const auto& placement : resolved)
        if (placement.id == "gen-rock") foundGenerated = true;
    CHECK_FALSE(foundGenerated);
}

TEST_CASE("M15 authored: persistent player mods survive regeneration")
{
    CellKey cell{0, 0, 0};
    ContentPlacement rock;
    rock.id = "mined-rock";
    rock.cell = cell;
    rock.kind = ContentKind::Asteroid;

    LayeredGeneration layered;
    auto base = layered.resolve(cell, {rock});
    CHECK(base.size() == 1);

    layered.setPlayerMod("mined-rock", PlayerModType::Depleted);
    CHECK(layered.hasPlayerMod("mined-rock"));
    auto after = layered.resolve(cell, {rock});
    CHECK(after.empty()); // regenerated rock is removed by the persistent mod

    layered.clearPlayerMod("mined-rock");
    auto restored = layered.resolve(cell, {rock});
    CHECK(restored.size() == 1);
}

TEST_CASE("M15 jumping: range gate and system transition")
{
    JumpDrive drive;
    drive.setRangeLy(2.0f);
    CHECK(drive.canJump(2.0f));
    CHECK_FALSE(drive.canJump(2.1f));
    CHECK_FALSE(drive.canJump(-1.0f));
    CHECK_FALSE(drive.jump("alpha", 3.0f).has_value()); // out of range

    auto result = drive.jump("alpha", 1.5f);
    REQUIRE(result.has_value());
    CHECK(result->fromSystem == "sol");
    CHECK(result->toSystem == "alpha");
    CHECK(drive.currentSystem() == "alpha");
    CHECK_FALSE(drive.jump("", 1.0f).has_value()); // empty target
}

// ---------------------------------------------------------------------------
// 15.2 / 15.5 Docking (enables the trading loop)
// ---------------------------------------------------------------------------

TEST_CASE("M15 docking: approach, dock, undock")
{
    DockingController dock;
    CHECK(dock.state() == DockState::Flying);

    dock.update(300.0, 10.0, "station-a");
    CHECK(dock.state() == DockState::Flying); // too far for approach
    CHECK_FALSE(dock.requestDock("station-a", "slot-1"));

    dock.update(150.0, 10.0, "station-a");
    CHECK(dock.state() == DockState::Approach);
    CHECK_FALSE(dock.requestDock("station-a", "slot-1")); // still too far/too fast

    dock.update(30.0, 5.0, "station-a");
    REQUIRE(dock.requestDock("station-a", "slot-1"));
    CHECK(dock.state() == DockState::Docked);
    CHECK(dock.stationId() == "station-a");
    CHECK(dock.dockSlot() == "slot-1");

    dock.undock();
    CHECK(dock.state() == DockState::Flying);
    CHECK(dock.stationId().empty());
}
