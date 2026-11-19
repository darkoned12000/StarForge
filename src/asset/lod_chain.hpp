#pragma once

// M17.4: asset-level LOD chains. Levels are handles into an AssetManager;
// selection is pure CPU with an optional hysteresis-backed selector.

#include "asset_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace starforge::asset
{

struct LodLevel
{
    AssetHandle asset;
    std::uint32_t meshIndex = 0;
    float maxDistance = 0.0f; // metres; level is used while distance <= this
};

class LodChain
{
public:
    void add(AssetHandle asset, std::uint32_t meshIndex, float maxDistance);
    [[nodiscard]] std::size_t size() const noexcept { return levels_.size(); }
    [[nodiscard]] const std::vector<LodLevel>& levels() const noexcept { return levels_; }

    // Returns the finest level whose maxDistance covers `distance` (or the
    // coarsest level when nothing covers it). nullptr for an empty chain.
    [[nodiscard]] const LodLevel* select(float distance) const noexcept;

private:
    std::vector<LodLevel> levels_;
};

class LodSelector
{
public:
    explicit LodSelector(const LodChain& chain) noexcept : chain_(chain) {}

    // Stateful selection with a hysteresis band, so an object hovering at a
    // threshold does not flicker between two LODs.
    [[nodiscard]] const LodLevel* update(float distance, float hysteresis) noexcept;
    [[nodiscard]] std::size_t currentIndex() const noexcept { return current_; }
    void reset() noexcept { current_ = 0; }

private:
    const LodChain& chain_;
    std::size_t current_ = 0;
};

} // namespace starforge::asset
