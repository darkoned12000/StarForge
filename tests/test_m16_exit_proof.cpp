// M16.10 (T5): the full gameplay loop driven through UI hit-testing and
// click -> action mapping, with a 5-minute frame-budget gate.
#include <doctest/doctest.h>

#include "game_simulation.hpp"
#include "ui_comms.hpp"
#include "ui_map.hpp"
#include "ui_station.hpp"
#include "ui_state.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

using namespace starforge::game;

namespace
{
// Clicks a station button through the same UiState hit-test path the game
// uses: register widget geometry, supply a cursor over its centre, then read
// the click edge and dispatch via applyStationAction.
void clickButton(UiState& ui, GameSimulation& sim, CommsLog& comms, const std::string& id)
{
    const auto buttons = stationButtons();
    const auto it = std::find_if(buttons.begin(), buttons.end(),
                                 [&](const auto& b) { return b.id == id; });
    REQUIRE(it != buttons.end());
    const auto& button = *it;
    const auto panel = stationPanelRect(1920.0f, 1080.0f);
    ui.beginFrame(panel.x + button.x + button.w * 0.5f,
                  panel.y + button.y + button.h * 0.5f, true);
    for (const auto& b : buttons)
        ui.addWidget(b.id, UiRect{panel.x + b.x, panel.y + b.y, b.w, b.h});
    REQUIRE(ui.clicked(id));
    REQUIRE(applyStationAction(id, sim, comms));
}
} // namespace

TEST_CASE("M16.10: full trading loop driven by UI clicks, repeatable")
{
    GameSimulation sim;
    CommsLog comms;
    UiState ui;
    const int startCredits = sim.credits();

    for (int round = 0; round < 2; ++round)
    {
        clickButton(ui, sim, comms, "dock");
        CHECK(sim.isDocked());
        clickButton(ui, sim, comms, "buy-5");
        CHECK(sim.cargoCount("ore") == 5);
        clickButton(ui, sim, comms, "undock");
        CHECK_FALSE(sim.isDocked());
        clickButton(ui, sim, comms, "dock");
        clickButton(ui, sim, comms, "sell-5");
        CHECK(sim.cargoCount("ore") == 0);
        CHECK(sim.credits() == startCredits);
    }
    CHECK(comms.count() > 0); // UI actions produced visible messages
}

TEST_CASE("M16.10: mission + jump loop driven by UI clicks")
{
    GameSimulation sim;
    CommsLog comms;
    UiState ui;
    clickButton(ui, sim, comms, "accept");
    clickButton(ui, sim, comms, "dock");
    clickButton(ui, sim, comms, "buy-5");
    clickButton(ui, sim, comms, "buy-5");
    CHECK(sim.cargoCount("ore") == 10);
    clickButton(ui, sim, comms, "complete");
    CHECK(sim.completedMissions() == 1);
    clickButton(ui, sim, comms, "jump");
    CHECK(sim.currentSystem() == "alpha-centauri");

    // Map projection reflects the new system and unlocked range.
    const auto& systems = knownSystems();
    const auto it = std::find_if(systems.begin(), systems.end(),
                                 [&](const auto& s) { return s.id == sim.currentSystem(); });
    REQUIRE(it != systems.end());
    CHECK(projectSystem(*it, *it, 100.0f, sim.jumpRangeLy()).reachable);
}

TEST_CASE("M16.10: five-minute UI-driven simulation under ADR-004 interval")
{
    GameSimulation sim;
    CommsLog comms;
    UiState ui;
    const RawVec3f forward{0.0f, 0.0f, -1.0f};
    std::vector<double> timings;
    const int totalSteps = 5 * 60 * 60;
    timings.reserve(6000);

    for (int i = 0; i < totalSteps; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        (void)sim.step(1.0f / 60.0f, forward, (i % 20) < 3, false);
        comms.tick(1.0f / 60.0f);
        if (i % 600 == 0)
        {
            clickButton(ui, sim, comms, "buy-5");
            clickButton(ui, sim, comms, "sell-5");
        }
        const auto end = std::chrono::steady_clock::now();
        if (i >= 600)
            timings.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(timings.begin(), timings.end());
    CHECK(timings[(timings.size() * 95) / 100] < 6.94);
}
