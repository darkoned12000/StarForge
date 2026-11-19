#include "ui_map.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::game
{

MapPoint projectSystem(const MapSystem& system, const MapSystem& origin,
                       float pixelsPerUnit, float jumpRange) noexcept
{
    MapPoint out;
    const float dx = system.x - origin.x;
    const float dy = system.y - origin.y;
    out.distance = std::sqrt(dx * dx + dy * dy);
    out.x = dx * pixelsPerUnit;
    out.y = dy * pixelsPerUnit;
    out.reachable = out.distance <= jumpRange;
    return out;
}

MapLayout mapLayout(float windowWidth, float windowHeight,
                    const std::string& currentSystem, float jumpRange)
{
    MapLayout layout;
    layout.panel = UiRect{windowWidth * 0.5f - 300.0f, windowHeight * 0.5f - 190.0f,
                          600.0f, 380.0f};
    layout.centreX = layout.panel.x + 190.0f;
    layout.centreY = layout.panel.y + 190.0f;
    const auto& systems = knownSystems();
    auto originIt = std::find_if(systems.begin(), systems.end(),
                                 [&](const auto& s) { return s.id == currentSystem; });
    if (originIt == systems.end()) originIt = systems.begin();
    for (const auto& sys : systems)
    {
        const auto p = projectSystem(sys, *originIt, 110.0f, jumpRange);
        MapLayout::SystemHit hit;
        hit.id = sys.id;
        hit.point = p;
        hit.current = sys.id == currentSystem;
        hit.rect = UiRect{layout.centreX + p.x - 6.0f, layout.centreY + p.y - 6.0f, 12.0f, 12.0f};
        layout.systems.push_back(hit);
    }
    layout.jumpButton = UiRect{layout.panel.x + 390.0f, layout.panel.y + 310.0f,
                               190.0f, 40.0f};
    return layout;
}

std::vector<SectorObject> sectorObjects(const std::string& systemId)
{
    if (systemId == "sol")
        return {{"station-a", "station", -0.4f, 0.3f},
                {"terra", "planet", 0.6f, -0.2f},
                {"pirate-1", "hostile", 1.1f, 0.5f},
                {"field-1", "rock", -1.0f, -0.8f}};
    if (systemId == "alpha-centauri")
        return {{"station-b", "station", 0.2f, -0.5f},
                {"ac-primary", "planet", -0.7f, 0.4f}};
    return {{"beacon", "station", 0.0f, 0.0f}};
}

const std::vector<MapSystem>& knownSystems() noexcept
{
    static const std::vector<MapSystem> systems = {
        {"sol", 0.0f, 0.0f},
        {"alpha-centauri", 1.3f, 0.4f},
        {"barnard", -0.9f, 0.8f},
        {"wolf-359", 0.3f, -1.4f},
        {"proxima", 2.2f, -0.6f},
    };
    return systems;
}

} // namespace starforge::game
