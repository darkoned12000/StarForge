#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace starforge::gameplay
{

struct CellKey
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    auto operator<=>(const CellKey&) const = default;
    bool operator==(const CellKey&) const = default;
};

enum class ContentKind
{
    Asteroid,
    Station,
    Encounter,
    Planet,
    MissionObjective,
};

// A single placed piece of content. `id` is globally unique so persistent
// player modifications can address it across regeneration.
struct ContentPlacement
{
    std::string id;
    CellKey cell;
    ContentKind kind = ContentKind::Asteroid;
    std::uint64_t seed = 0;
    int priority = 0; // higher priority wins between authored placements
    double x = 0.0, y = 0.0, z = 0.0; // local offset within the cell
};

enum class PlayerModType
{
    Depleted,  // mined asteroid / exhausted resource
    Destroyed, // destroyed station
    Visited,   // discovered/visited location
    Captured,  // ownership / faction change
};

// Layered generation (M15 "procedural != gameplay-locked"):
//   procedural foundation -> authored overrides -> faction/economic placement
//   -> mission-specific additions -> persistent player modifications.
//
// This class owns the authored override layer and the persistent player
// modification overlay. `resolve` composes them over a generated set:
// authored placements of a given kind suppress generated ones of that kind in
// their cell; player Depleted/Destroyed mods remove content by id.
class LayeredGeneration
{
public:
    void addAuthored(ContentPlacement placement);
    [[nodiscard]] std::size_t authoredCount() const noexcept { return authored_.size(); }

    void setPlayerMod(const std::string& targetId, PlayerModType type);
    void clearPlayerMod(const std::string& targetId);
    [[nodiscard]] bool hasPlayerMod(const std::string& targetId) const noexcept;
    [[nodiscard]] PlayerModType playerMod(const std::string& targetId) const noexcept;

    // Returns the final content for `cell` after all overlays are applied.
    [[nodiscard]] std::vector<ContentPlacement>
    resolve(const CellKey& cell, std::vector<ContentPlacement> generated) const;

    [[nodiscard]] std::vector<ContentPlacement> authoredIn(const CellKey& cell) const;

private:
    std::multimap<CellKey, ContentPlacement> authored_;
    std::map<std::string, PlayerModType> playerMods_;
};

} // namespace starforge::gameplay
