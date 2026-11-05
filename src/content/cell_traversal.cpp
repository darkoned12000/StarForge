#include "content/cell_traversal.hpp"
#include "spatial/world_position.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>


namespace starforge::content
{

namespace
{

// Add a small signed cell offset with saturation instead of signed overflow.
std::int64_t addCellOffset(std::int64_t base, int delta) noexcept
{
    if (delta > 0 && base > std::numeric_limits<std::int64_t>::max() - delta)
        return std::numeric_limits<std::int64_t>::max();
    if (delta < 0 && base < std::numeric_limits<std::int64_t>::min() - delta)
        return std::numeric_limits<std::int64_t>::min();
    return base + delta;
}

} // namespace

CellCoord worldToCell(const starforge::spatial::WorldPosition& pos, int level)
{
    if (level < 0) level = 0;
    if (level > kMaxScaleLevel) level = kMaxScaleLevel;

    CellCoord coord;
    coord.level = level;
    // WorldPosition owns the authoritative floor-shift / collapse semantics.
    coord.x = pos.coarseCellX(level);
    coord.y = pos.coarseCellY(level);
    coord.z = pos.coarseCellZ(level);
    return coord;
}

std::vector<CellCoord> traverseCells(
    const starforge::spatial::WorldPosition& cameraPos,
    int minLevel,
    int maxLevel,
    double radiusMetres)
{
    std::vector<CellCoord> result;

    if (minLevel < 0) minLevel = 0;
    if (maxLevel > kMaxScaleLevel) maxLevel = kMaxScaleLevel;
    if (maxLevel < minLevel || !(radiusMetres >= 0.0))
        return result;

    for (int level = minLevel; level <= maxLevel; ++level)
    {
        const double cellSize = cellSizeAtLevel(level);

        // Camera cell at this level.
        CellCoord camCell = worldToCell(cameraPos, level);

        // Per-axis cell index range whose cell volume intersects the sphere
        // around the camera. Using the camera's position inside its coarse
        // cell avoids forcing a 3x3x3 neighbourhood for small radii.
        int loX = 0, hiX = 0;
        int loY = 0, hiY = 0;
        int loZ = 0, hiZ = 0;

        if (level < kMaxScaleLevel)
        {
            const int shift = starforge::spatial::WorldPosition::kCellBits * level;
            const std::uint64_t mask =
                (shift == 0) ? 0ULL : ((1ULL << shift) - 1ULL);

            auto axisRange = [&](std::int64_t cellCoord, double offset,
                                 int& lo, int& hi) {
                // Position of the camera within the coarse cell, in metres.
                const std::uint64_t sub =
                    static_cast<std::uint64_t>(cellCoord) & mask;
                const double inCell =
                    static_cast<double>(sub) * starforge::spatial::WorldPosition::kCellSize + offset;

                const double loD = std::floor((inCell - radiusMetres) / cellSize);
                const double hiD = std::floor((inCell + radiusMetres) / cellSize);

                // Cap the neighbourhood to keep traversal bounded.
                lo = static_cast<int>(std::max(-4.0, std::min(0.0, loD)));
                hi = static_cast<int>(std::max(0.0, std::min(4.0, hiD)));
            };

            axisRange(cameraPos.cellX, cameraPos.offsetX, loX, hiX);
            axisRange(cameraPos.cellY, cameraPos.offsetY, loY, hiY);
            axisRange(cameraPos.cellZ, cameraPos.offsetZ, loZ, hiZ);
        }
        // At kMaxScaleLevel the world is a single summary cell.

        for (int dx = loX; dx <= hiX; ++dx)
        {
            for (int dy = loY; dy <= hiY; ++dy)
            {
                for (int dz = loZ; dz <= hiZ; ++dz)
                {
                    CellCoord coord;
                    coord.x = addCellOffset(camCell.x, dx);
                    coord.y = addCellOffset(camCell.y, dy);
                    coord.z = addCellOffset(camCell.z, dz);
                    coord.level = level;

                    result.push_back(coord);
                }
            }
        }
    }

    return result;
}

} // namespace starforge::content
