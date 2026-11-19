#pragma once

#include <set>
#include <string>

namespace starforge::gameplay
{

// Persistent progression state (owned by the player, preserved across jumps
// and intended to survive through M18 persistence). Unlocks are keyed strings;
// a small set of well-known keys map to concrete capability changes.
class Progression
{
public:
    void applyUnlock(const std::string& key);
    [[nodiscard]] bool has(const std::string& key) const noexcept;

    [[nodiscard]] float jumpRangeLy() const noexcept { return jumpRangeLy_; }
    void setJumpRangeLy(float range) noexcept { jumpRangeLy_ = range; }

    [[nodiscard]] int completedMissions() const noexcept { return completedMissions_; }
    void setCompletedMissions(int count) noexcept { completedMissions_ = count; }

    [[nodiscard]] const std::set<std::string>& unlocks() const noexcept { return unlocks_; }

private:
    std::set<std::string> unlocks_;
    float jumpRangeLy_ = 1.0f;
    int completedMissions_ = 0;
};

} // namespace starforge::gameplay
