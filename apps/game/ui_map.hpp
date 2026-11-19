#pragma once

// M16.9: pure-CPU galaxy/sector map projection. Map coordinates are a
// lightweight 2D layout, not astronomical WorldPositions.
#include <cstddef>
#include <string>
#include <vector>

namespace starforge::game
{

struct MapSystem
{
    std::string id;
    float x = 0.0f;
    float y = 0.0f;
};

struct MapPoint
{
    float x = 0.0f; // pixels from map centre
    float y = 0.0f;
    float distance = 0.0f; // map units from the current system
    bool reachable = false;
};

// Projects a system into a map centred on `originId`, scaled by px per unit,
// with reachability derived from the player's jump range.
[[nodiscard]] MapPoint projectSystem(const MapSystem& system, const MapSystem& origin,
                                     float pixelsPerUnit, float jumpRange) noexcept;

[[nodiscard]] const std::vector<MapSystem>& knownSystems() noexcept;

} // namespace starforge::game
