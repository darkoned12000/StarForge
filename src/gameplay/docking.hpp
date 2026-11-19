#pragma once

#include <string>

namespace starforge::gameplay
{

enum class DockState
{
    Flying,
    Approach, // within approach radius, request prompt shown
    Docked,
};

// Pure-CPU docking approach controller. The game layer measures the ship's
// distance and speed relative to a station and feeds them in each tick; the
// controller decides approach state and whether a dock request succeeds.
class DockingController
{
public:
    void setApproachRadius(double radius) noexcept { approachRadius_ = radius; }
    void setDockRadius(double radius) noexcept { dockRadius_ = radius; }
    void setMaxDockSpeed(double speed) noexcept { maxDockSpeed_ = speed; }

    [[nodiscard]] double approachRadius() const noexcept { return approachRadius_; }
    [[nodiscard]] double dockRadius() const noexcept { return dockRadius_; }
    [[nodiscard]] double maxDockSpeed() const noexcept { return maxDockSpeed_; }

    [[nodiscard]] DockState state() const noexcept { return state_; }
    [[nodiscard]] const std::string& stationId() const noexcept { return stationId_; }
    [[nodiscard]] const std::string& dockSlot() const noexcept { return dockSlot_; }

    // Updates approach state from ship/station kinematics. Returns the state.
    DockState update(double distance, double speed, const std::string& nearbyStation);

    // Attempts to dock. Requires an approach state, close distance and low
    // speed. `slot` is the dock slot identifier at the station.
    [[nodiscard]] bool requestDock(const std::string& station, const std::string& slot);
    void undock() noexcept;

private:
    double approachRadius_ = 200.0;
    double dockRadius_ = 40.0;
    double maxDockSpeed_ = 25.0;
    double distance_ = 0.0;
    double speed_ = 0.0;
    DockState state_ = DockState::Flying;
    std::string nearbyStation_;
    std::string stationId_;
    std::string dockSlot_;
};

} // namespace starforge::gameplay
