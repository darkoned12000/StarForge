#include "lod_chain.hpp"

#include <algorithm>

namespace starforge::asset
{

void LodChain::add(AssetHandle asset, std::uint32_t meshIndex, float maxDistance)
{
    levels_.push_back({asset, meshIndex, maxDistance});
    std::stable_sort(levels_.begin(), levels_.end(),
                     [](const LodLevel& a, const LodLevel& b) {
                         return a.maxDistance < b.maxDistance;
                     });
}

const LodLevel* LodChain::select(float distance) const noexcept
{
    if (levels_.empty()) return nullptr;
    for (const auto& level : levels_)
        if (distance <= level.maxDistance) return &level;
    return &levels_.back();
}

const LodLevel* LodSelector::update(float distance, float hysteresis) noexcept
{
    const auto& levels = chain_.levels();
    if (levels.empty()) return nullptr;
    if (current_ >= levels.size()) current_ = levels.size() - 1;

    const float currentLimit = levels[current_].maxDistance;
    if (distance > currentLimit + hysteresis &&
        current_ + 1 < levels.size())
    {
        ++current_;
    }
    else if (current_ > 0)
    {
        // Switch to a finer level only well inside its threshold.
        const float finerLimit = levels[current_ - 1].maxDistance;
        if (distance + hysteresis < finerLimit) --current_;
    }
    return &levels[current_];
}

} // namespace starforge::asset
