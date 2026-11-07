#pragma once

#include "math/math.hpp"

#include <cstdint>
#include <vector>

namespace starforge::planets
{

// Represents a single patch of planetary surface geometry
struct PlanetPatch
{
    // Patch coordinates in the quadtree (x, y) and level 
    int32_t x;
    int32_t y;
    uint32_t level;

    // Bounding sphere for frustum culling
    float center[3];
    float radius;

    // Material index for this patch
    std::uint32_t materialIndex;
    
    // Patch data offset in the vertex buffer (for GPU access)
    std::uint32_t vertexOffset;
};

// Planet quadtree structure for LOD management
class PlanetQuadtree
{
public:
    explicit PlanetQuadtree(float radius, float maxError);
    
    // Generate patches at a specific level of detail
    std::vector<PlanetPatch> generatePatches(int32_t x, int32_t y, uint32_t level) const;
    
    // Get the maximum level of detail for this planet
    uint32_t getMaxLevel() const;

private:
    float radius_;
    float maxError_;
    uint32_t maxLevel_;
};

} // namespace starforge::planets