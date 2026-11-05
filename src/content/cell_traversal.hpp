#pragma once

#include "spatial/world_position.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace starforge::content
{

// Highest scale level with a distinct content grid.
// Levels 0-3 fit the int64 cell index (20 bits per level). At level 4 and
// above the whole representable world collapses to a single coarse cell, so
// the level is treated as a final, non-subdividable summary level.
inline constexpr int kMaxScaleLevel = 4;

// Side length of a cell at a given scale level, in metres.
// Level 0 is one WorldPosition cell (2^20 m); each level multiplies by 2^20.
[[nodiscard]] inline double cellSizeAtLevel(int level) noexcept
{
    return std::ldexp(1.0, starforge::spatial::WorldPosition::kCellBits * (level + 1));
}

// A cell coordinate at a given scale level.
struct CellCoord
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    int level = 0;
};

// Traverse the power-of-two cell hierarchy around a camera position.
// Returns cells at levels [minLevel, maxLevel] whose cell volume intersects
// the sphere of the given radius. At kMaxScaleLevel the hierarchy collapses
// and only the camera's single summary cell is returned.
// Radius is in metres. Levels are clamped to [0, kMaxScaleLevel].
[[nodiscard]] std::vector<CellCoord> traverseCells(
    const starforge::spatial::WorldPosition& cameraPos,
    int minLevel,
    int maxLevel,
    double radiusMetres);

// Convert a WorldPosition to its cell coordinate at a given level.
// Delegates to WorldPosition::coarseCell*, so level >= kMaxScaleLevel
// collapses consistently with the rest of spatial code. Levels are clamped
// to [0, kMaxScaleLevel].
[[nodiscard]] CellCoord worldToCell(
    const starforge::spatial::WorldPosition& pos,
    int level);

} // namespace starforge::content
