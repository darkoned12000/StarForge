#include "gameplay/progression.hpp"

#include <algorithm>

namespace starforge::gameplay
{

void Progression::applyUnlock(const std::string& key)
{
    // Idempotent: re-awarding an unlock must not shrink capabilities.
    if (unlocks_.contains(key)) return;
    unlocks_.insert(key);
    if (key == "jump2") jumpRangeLy_ = std::max(jumpRangeLy_, 2.0f);
    else if (key == "jump5") jumpRangeLy_ = std::max(jumpRangeLy_, 5.0f);
    else if (key == "jump10") jumpRangeLy_ = std::max(jumpRangeLy_, 10.0f);
}

bool Progression::has(const std::string& key) const noexcept
{
    return unlocks_.contains(key);
}

} // namespace starforge::gameplay
