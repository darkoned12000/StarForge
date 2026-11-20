#pragma once

// M16.8: station screen layout + click-to-action mapping (pure CPU).
#include "game_simulation.hpp"
#include "ui_comms.hpp"
#include "ui_state.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace starforge::game
{

struct StationButton
{
    std::string id;
    std::string label;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// Station panel position/size derived from window dimensions.
[[nodiscard]] UiRect stationPanelRect(float windowWidth, float windowHeight);

// Button geometry relative to the station panel origin.
[[nodiscard]] std::vector<StationButton> stationButtons();

// Applies a clicked button id to the simulation. Returns true if handled.
// `stationDistance` is the real ship-to-station range in metres; dock only
// succeeds when the ship is actually close (tests may pass the default).
bool applyStationAction(std::string_view id, GameSimulation& simulation, CommsLog& comms,
                        double stationDistance = 20.0);

} // namespace starforge::game
