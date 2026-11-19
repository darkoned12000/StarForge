// M16.8 (T1): station buttons map to real gameplay actions.
#include <doctest/doctest.h>

#include "ui_station.hpp"

using namespace starforge::game;

TEST_CASE("M16.8: station button layout has unique clickable ids")
{
    const auto buttons = stationButtons();
    REQUIRE(buttons.size() >= 6);
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        CHECK_FALSE(buttons[i].id.empty());
        CHECK_FALSE(buttons[i].label.empty());
        CHECK(buttons[i].w > 0.0f);
        CHECK(buttons[i].h > 0.0f);
        for (std::size_t j = 0; j < i; ++j)
            CHECK(buttons[i].id != buttons[j].id);
    }
}

TEST_CASE("M16.8: buy/sell clicks change cargo and credits")
{
    GameSimulation sim;
    CommsLog comms;
    const int startCredits = sim.credits();

    REQUIRE(applyStationAction("dock", sim, comms));
    CHECK(sim.isDocked());
    REQUIRE(applyStationAction("buy-5", sim, comms));
    CHECK(sim.cargoCount("ore") == 5);
    CHECK(sim.credits() == startCredits - 100);
    REQUIRE(applyStationAction("sell-5", sim, comms));
    CHECK(sim.cargoCount("ore") == 0);
    CHECK(sim.credits() == startCredits);
    REQUIRE(applyStationAction("undock", sim, comms));
    CHECK_FALSE(sim.isDocked());
    CHECK_FALSE(applyStationAction("unknown", sim, comms));
}

TEST_CASE("M16.8: mission and jump clicks use the simulation")
{
    GameSimulation sim;
    CommsLog comms;
    REQUIRE(applyStationAction("accept", sim, comms));
    REQUIRE(applyStationAction("buy-5", sim, comms));
    // Need 10 ore for the contract; buy a second batch through the action.
    REQUIRE(applyStationAction("buy-5", sim, comms));
    REQUIRE(applyStationAction("complete", sim, comms));
    CHECK(sim.completedMissions() == 1);
    REQUIRE(applyStationAction("jump", sim, comms));
    CHECK(sim.currentSystem() == "alpha-centauri");
}
