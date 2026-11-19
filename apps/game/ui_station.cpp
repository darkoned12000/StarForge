#include "ui_station.hpp"

namespace starforge::game
{

UiRect stationPanelRect(float windowWidth, float windowHeight)
{
    return UiRect{windowWidth * 0.5f - 420.0f, windowHeight * 0.5f - 160.0f, 280.0f, 320.0f};
}

std::vector<StationButton> stationButtons()
{
    return {{"dock", "DOCK", 20.0f, 40.0f, 110.0f, 34.0f},
            {"undock", "UNDOCK", 140.0f, 40.0f, 110.0f, 34.0f},
            {"buy-5", "BUY 5 ORE", 20.0f, 90.0f, 110.0f, 34.0f},
            {"sell-5", "SELL 5 ORE", 140.0f, 90.0f, 110.0f, 34.0f},
            {"accept", "ACCEPT HAUL", 20.0f, 140.0f, 230.0f, 34.0f},
            {"complete", "COMPLETE HAUL", 20.0f, 190.0f, 230.0f, 34.0f},
            {"jump", "JUMP", 20.0f, 240.0f, 230.0f, 34.0f}};
}

bool applyStationAction(std::string_view id, GameSimulation& simulation, CommsLog& comms)
{
    if (id == "dock")
    {
        simulation.updateDocking(20.0, 0.0, "station-a");
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
        if (simulation.jump("alpha-centauri", 1.5f))
            comms.push(CommsChannel::System, "nav", "Jumped to alpha-centauri");
        else
            comms.push(CommsChannel::System, "nav", "Jump range insufficient");
        return true;
    }
    return false;
}

} // namespace starforge::game
