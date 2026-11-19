#pragma once

// M16.9: pure-CPU galaxy/sector map projection. Map coordinates are a
// lightweight 2D layout, not astronomical WorldPositions.
#include "ui_state.hpp"

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

// Drill-in: notable local objects for a selected system.
struct SectorObject
{
    std::string id;
    std::string kind; // "station", "planet", "hostile", "rock"
    float x = 0.0f;
    float y = 0.0f;
};

[[nodiscard]] std::vector<SectorObject> sectorObjects(const std::string& systemId);

// Label for the map's primary action button.
[[nodiscard]] std::string jumpButtonLabel(const std::string& selectedSystem,
                                          const std::string& currentSystem);

// Shared layout for hit-testing and rendering the map. The panel is split
// into a left-hand star chart and a right-hand information section so text
// can never overlap the chart.
struct MapLayout
{
    UiRect panel{};
    UiRect mapSection{};
    UiRect infoSection{};
    float centreX = 0.0f;
    float centreY = 0.0f;
    float dividerX = 0.0f;
    struct SystemHit
    {
        std::string id;
        UiRect rect;
        MapPoint point;
        bool current = false;
    };
    std::vector<SystemHit> systems;
    UiRect jumpButton{};
};

[[nodiscard]] MapLayout mapLayout(float windowWidth, float windowHeight,
                                  const std::string& currentSystem, float jumpRange);

} // namespace starforge::game
