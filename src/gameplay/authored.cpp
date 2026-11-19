#include "gameplay/authored.hpp"

#include <algorithm>
#include <array>

namespace starforge::gameplay
{

void LayeredGeneration::addAuthored(ContentPlacement placement)
{
    authored_.emplace(placement.cell, std::move(placement));
}

void LayeredGeneration::setPlayerMod(const std::string& targetId, PlayerModType type)
{
    playerMods_[targetId] = type;
}

void LayeredGeneration::clearPlayerMod(const std::string& targetId)
{
    playerMods_.erase(targetId);
}

bool LayeredGeneration::hasPlayerMod(const std::string& targetId) const noexcept
{
    return playerMods_.contains(targetId);
}

PlayerModType LayeredGeneration::playerMod(const std::string& targetId) const noexcept
{
    auto it = playerMods_.find(targetId);
    return it != playerMods_.end() ? it->second : PlayerModType::Visited;
}

std::vector<ContentPlacement> LayeredGeneration::authoredIn(const CellKey& cell) const
{
    std::vector<ContentPlacement> out;
    auto range = authored_.equal_range(cell);
    for (auto it = range.first; it != range.second; ++it) out.push_back(it->second);
    return out;
}

std::vector<ContentPlacement>
LayeredGeneration::resolve(const CellKey& cell, std::vector<ContentPlacement> generated) const
{
    // Layer: authored overrides suppress generated content of the same kind
    // within this cell.
    const auto authored = authoredIn(cell);
    std::array<bool, static_cast<std::size_t>(ContentKind::MissionObjective) + 1> authoredKinds{};
    for (const auto& placement : authored)
        authoredKinds[static_cast<std::size_t>(placement.kind)] = true;

    generated.erase(
        std::remove_if(generated.begin(), generated.end(), [&](const ContentPlacement& placement) {
            return authoredKinds[static_cast<std::size_t>(placement.kind)];
        }),
        generated.end());

    // Authored placements win; higher priority sorts first.
    std::vector<ContentPlacement> result = generated;
    result.insert(result.end(), authored.begin(), authored.end());
    std::stable_sort(result.begin(), result.end(), [](const ContentPlacement& a, const ContentPlacement& b) {
        return a.priority > b.priority;
    });

    // Layer: persistent player modifications remove depleted/destroyed content.
    result.erase(
        std::remove_if(result.begin(), result.end(), [&](const ContentPlacement& placement) {
            auto it = playerMods_.find(placement.id);
            if (it == playerMods_.end()) return false;
            return it->second == PlayerModType::Depleted || it->second == PlayerModType::Destroyed;
        }),
        result.end());

    return result;
}

} // namespace starforge::gameplay
