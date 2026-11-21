#include "gameplay/docking.hpp"

#include <cmath>

namespace starforge::gameplay
{

DockState DockingController::update(double distance, double speed, const std::string& nearbyStation)
{
    distance_ = distance;
    speed_ = speed;
    nearbyStation_ = nearbyStation;

    if (state_ == DockState::Docked) return state_;
    const bool validKinematics =
        std::isfinite(distance_) && std::isfinite(speed_) && distance_ >= 0.0 && speed_ >= 0.0;
    if (validKinematics && !nearbyStation.empty() && distance_ <= approachRadius_)
        state_ = DockState::Approach;
    else
        state_ = DockState::Flying;
    return state_;
}

bool DockingController::requestDock(const std::string& station, const std::string& slot)
{
    if (state_ != DockState::Approach) return false;
    if (station.empty() || station != nearbyStation_) return false;
    if (!std::isfinite(distance_) || !std::isfinite(speed_)) return false;
    if (distance_ < 0.0 || speed_ < 0.0) return false;
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
