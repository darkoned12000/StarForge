#include "ui_map.hpp"

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
