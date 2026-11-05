#pragma once

#include "math/math.hpp"

#include <vector>
#include <cstdint>

namespace starforge::planets
{

struct PlanetPatchVertex
{
    float x, y, z;
};

// Generate a grid mesh for a planet patch (33x33 vertices by default).
// Returns vertices in local patch space.
std::vector<PlanetPatchVertex> generatePatchGrid(int resolution = 33);

// Generate patch mesh with terrain skirts for crack elimination.
// Skirts extrude boundary vertices downward (negative Z) in local tangent space.
std::vector<PlanetPatchVertex> generatePatchGridWithSkirts(int resolution = 33, float skirtDepth = 0.1f);

// Generate index buffer for a patch grid.
// Produces triangle indices for internal quads and skirt strips.
std::vector<std::uint32_t> generatePatchIndices(int resolution, bool withSkirts = true);

} // namespace starforge::planets
