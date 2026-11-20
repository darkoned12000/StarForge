#include "ui_station.hpp"
#include "ui_map.hpp"

namespace starforge::game
{

UiRect stationPanelRect(float windowWidth, float windowHeight)
{
    constexpr float w = 360.0f;
    constexpr float h = 400.0f;
    return UiRect{windowWidth * 0.5f - w * 0.5f, windowHeight * 0.5f - h * 0.5f, w, h};
}

StationPrompt stationPromptRect(float windowWidth, float windowHeight)
{
    (void)windowWidth;
    // Comms panel sits at y = H-232 with height 216; keep the prompt directly
    // above it with a small gap.
    const float py = windowHeight - 232.0f - 64.0f;
    StationPrompt prompt;
    prompt.panel = UiRect{16.0f, py, 420.0f, 58.0f};
    prompt.dock = UiRect{26.0f, py + 24.0f, 190.0f, 28.0f};
    prompt.chat = UiRect{236.0f, py + 24.0f, 190.0f, 28.0f};
    return prompt;
}

std::vector<StationButton> stationButtons()
{
    // Trade / mission station interface, reached through the proximity dock
    // prompt rather than the old Space toggle.
    return {{"close", "CLOSE", 224.0f, 10.0f, 118.0f, 30.0f},
            {"buy-5", "BUY 5 ORE", 18.0f, 70.0f, 155.0f, 38.0f},
            {"sell-5", "SELL 5 ORE", 187.0f, 70.0f, 155.0f, 38.0f},
            {"dock", "DOCK", 18.0f, 116.0f, 155.0f, 38.0f},
            {"accept", "ACCEPT HAUL", 18.0f, 164.0f, 324.0f, 38.0f},
            {"complete", "COMPLETE HAUL", 18.0f, 208.0f, 324.0f, 38.0f},
            {"jump", "JUMP TO ALPHA-C", 18.0f, 264.0f, 324.0f, 38.0f},
            {"undock", "UNDOCK", 18.0f, 324.0f, 324.0f, 38.0f}};
}

bool applyStationAction(std::string_view id, GameSimulation& simulation, CommsLog& comms,
                        double stationDistance)
{
    if (id == "dock")
    {
        simulation.updateDocking(stationDistance, 0.0, "station-a");
        if (simulation.dock("station-a", "slot-1"))
            comms.push(CommsChannel::Trade, "station-a", "Docking granted");
        return true;
    }
    if (id == "undock")
    {
        simulation.undock();
        return true;
    }
    if (id == "buy-5")
    {
        if (simulation.buy("station-a", "ore", 5))
            comms.push(CommsChannel::Trade, "station-a", "Bought 5 ore");
        else
            comms.push(CommsChannel::System, "system", simulation.lastTradeError());
        return true;
    }
    if (id == "sell-5")
    {
        if (simulation.sell("station-a", "ore", 5))
            comms.push(CommsChannel::Trade, "station-a", "Sold 5 ore");
        else
            comms.push(CommsChannel::System, "system", simulation.lastTradeError());
        return true;
    }
    if (id == "accept")
    {
        if (simulation.acceptMission("haul-ore"))
            comms.push(CommsChannel::System, "missions", "Contract accepted: Ore Haul");
        return true;
    }
    if (id == "complete")
    {
        if (simulation.deliverMission("haul-ore", "ore", 10, "station-b") &&
            simulation.completeMission("haul-ore", "station-b"))
            comms.push(CommsChannel::System, "missions", "Ore Haul complete: +500 cr");
        else
            comms.push(CommsChannel::System, "missions", "Deliver 10 ore to station-b first");
        return true;
    }
    if (id == "jump")
    {
        const float distance = starforge::game::systemDistanceLy(
            simulation.currentSystem(), "alpha-centauri");
        if (distance > 0.0f && simulation.jump("alpha-centauri", distance))
            comms.push(CommsChannel::System, "nav", "Jumped to alpha-centauri");
        else
            comms.push(CommsChannel::System, "nav", "Jump range insufficient");
        return true;
    }
    return false;
}

} // namespace starforge::game
