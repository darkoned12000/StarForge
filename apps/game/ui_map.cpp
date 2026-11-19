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

float systemDistanceLy(const std::string& from, const std::string& to) noexcept
{
    const auto& systems = knownSystems();
    const auto a = std::find_if(systems.begin(), systems.end(),
                                [&](const auto& s) { return s.id == from; });
    const auto b = std::find_if(systems.begin(), systems.end(),
                                [&](const auto& s) { return s.id == to; });
    if (a == systems.end() || b == systems.end()) return 0.0f;
    const float dx = b->x - a->x;
    const float dy = b->y - a->y;
    return std::sqrt(dx * dx + dy * dy);
}

SystemVisual systemVisual(const std::string& systemId)
{
    SystemVisual v;
    if (systemId == "alpha-centauri")
    {
        v.nebulaColor = {0.08f, 0.32f, 0.30f, 1.0f};
        v.planetBase = {0.12f, 0.62f, 0.42f, 1.0f};
        v.planetShadow = {0.05f, 0.28f, 0.20f, 1.0f};
        v.planetRim = {0.55f, 1.00f, 0.75f, 1.0f};
        v.sunCore = {1.00f, 1.00f, 0.92f, 1.0f};
        v.sunLimb = {0.75f, 0.90f, 0.55f, 1.0f};
        v.sunCorona = {0.70f, 1.00f, 0.65f, 1.0f};
        v.planetRadius = 5200.0f;
        v.coronaIntensity = 0.85f;
        v.description = "Emerald twin suns - temperate trade world";
    }
    else if (systemId == "barnard")
    {
        v.nebulaColor = {0.38f, 0.10f, 0.08f, 1.0f};
        v.planetBase = {0.72f, 0.22f, 0.12f, 1.0f};
        v.planetShadow = {0.30f, 0.08f, 0.05f, 1.0f};
        v.planetRim = {1.00f, 0.55f, 0.30f, 1.0f};
        v.sunCore = {1.00f, 0.72f, 0.55f, 1.0f};
        v.sunLimb = {0.95f, 0.22f, 0.08f, 1.0f};
        v.sunCorona = {1.00f, 0.35f, 0.12f, 1.0f};
        v.planetRadius = 6800.0f;
        v.coronaIntensity = 1.30f;
        v.description = "Red dwarf flare star - arid mining outpost";
    }
    else if (systemId == "wolf-359")
    {
        v.nebulaColor = {0.10f, 0.14f, 0.42f, 1.0f};
        v.planetBase = {0.38f, 0.28f, 0.78f, 1.0f};
        v.planetShadow = {0.14f, 0.10f, 0.34f, 1.0f};
        v.planetRim = {0.75f, 0.65f, 1.00f, 1.0f};
        v.sunCore = {0.85f, 0.92f, 1.00f, 1.0f};
        v.sunLimb = {0.30f, 0.50f, 0.95f, 1.0f};
        v.sunCorona = {0.40f, 0.65f, 1.00f, 1.0f};
        v.planetRadius = 4400.0f;
        v.coronaIntensity = 0.70f;
        v.description = "Blue-white flare star - frozen research station";
    }
    else if (systemId == "proxima")
    {
        v.nebulaColor = {0.40f, 0.24f, 0.06f, 1.0f};
        v.planetBase = {0.85f, 0.48f, 0.18f, 1.0f};
        v.planetShadow = {0.34f, 0.18f, 0.06f, 1.0f};
        v.planetRim = {1.00f, 0.78f, 0.38f, 1.0f};
        v.sunCore = {1.00f, 0.88f, 0.68f, 1.0f};
        v.sunLimb = {1.00f, 0.42f, 0.10f, 1.0f};
        v.sunCorona = {1.00f, 0.55f, 0.15f, 1.0f};
        v.planetRadius = 7600.0f;
        v.coronaIntensity = 1.55f;
        v.description = "Volatile red giant - deep-space relay";
    }
    else // sol (default)
    {
        v.description = "Cradle system - blue homeworld and amber sun";
    }
    return v;
}

MapLayout mapLayout(float windowWidth, float windowHeight,
                    const std::string& currentSystem, float jumpRange)
{
    MapLayout layout;
    // Wide chart on the left, dedicated info column on the right.
    layout.panel = UiRect{windowWidth * 0.5f - 360.0f, windowHeight * 0.5f - 235.0f,
                          720.0f, 470.0f};
    layout.mapSection = UiRect{layout.panel.x + 8.0f, layout.panel.y + 44.0f,
                               452.0f, 414.0f};
    layout.infoSection = UiRect{layout.panel.x + 474.0f, layout.panel.y + 44.0f,
                                234.0f, 414.0f};
    layout.dividerX = layout.panel.x + 464.0f;
    layout.centreX = layout.mapSection.x + layout.mapSection.w * 0.5f;
    layout.centreY = layout.mapSection.y + layout.mapSection.h * 0.5f;

    const auto& systems = knownSystems();
    auto originIt = std::find_if(systems.begin(), systems.end(),
                                 [&](const auto& s) { return s.id == currentSystem; });
    if (originIt == systems.end()) originIt = systems.begin();
    constexpr float kPixelsPerUnit = 95.0f;
    for (const auto& sys : systems)
    {
        const auto p = projectSystem(sys, *originIt, kPixelsPerUnit, jumpRange);
        MapLayout::SystemHit hit;
        hit.id = sys.id;
        hit.point = p;
        hit.current = sys.id == currentSystem;
        // Hit box is a little larger than the drawn dot for easy clicking.
        hit.rect = UiRect{layout.centreX + p.x - 9.0f, layout.centreY + p.y - 9.0f,
                          18.0f, 18.0f};
        layout.systems.push_back(hit);
    }
    layout.jumpButton = UiRect{layout.infoSection.x + 12.0f,
                               layout.infoSection.y + layout.infoSection.h - 56.0f,
                               layout.infoSection.w - 24.0f, 44.0f};
    return layout;
}

std::string jumpButtonLabel(const std::string& selectedSystem,
                            const std::string& currentSystem)
{
    if (selectedSystem.empty() || selectedSystem == currentSystem)
        return "Select system";
    return "Jump to " + selectedSystem;
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
        {"alpha-centauri", 0.65f, 0.35f},
        {"barnard", -0.9f, 0.8f},
        {"wolf-359", 0.3f, -1.4f},
        {"proxima", 2.2f, -0.6f},
    };
    return systems;
}

} // namespace starforge::game
