#include "gameplay/docking.hpp"

namespace starforge::gameplay
{

DockState DockingController::update(double distance, double speed, const std::string& nearbyStation)
{
    distance_ = distance;
    speed_ = speed;
    nearbyStation_ = nearbyStation;

    if (state_ == DockState::Docked) return state_;
    if (!nearbyStation.empty() && distance <= approachRadius_)
        state_ = DockState::Approach;
    else
        state_ = DockState::Flying;
    return state_;
}

bool DockingController::requestDock(const std::string& station, const std::string& slot)
{
    if (state_ != DockState::Approach) return false;
    if (station.empty() || station != nearbyStation_) return false;
    if (distance_ > dockRadius_ || speed_ > maxDockSpeed_) return false;
    state_ = DockState::Docked;
    stationId_ = station;
    dockSlot_ = slot;
    return true;
}

void DockingController::undock() noexcept
{
    state_ = DockState::Flying;
    stationId_.clear();
    dockSlot_.clear();
}

} // namespace starforge::gameplay
