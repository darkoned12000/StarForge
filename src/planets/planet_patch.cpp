#include "planets/planet_patch.hpp"
#include "math/math.hpp"

namespace starforge::planets
{

PlanetQuadtree::PlanetQuadtree(float radius, float maxError) 
    : radius_(radius), maxError_(maxError)
{
    // Calculate maximum level based on error tolerance and planet size
    // This is a simplified approach - in practice this would be more sophisticated
    maxLevel_ = 10; // Arbitrary for now
}

std::vector<PlanetPatch> PlanetQuadtree::generatePatches(int32_t x, int32_t y, uint32_t level) const
{
    std::vector<PlanetPatch> patches;
    
    if (level > maxLevel_) return patches;
    
    // For a simple implementation, we'll create 4 patches per level
    // In reality this would be more complex with proper quadtree logic
    
    for (int32_t dy = 0; dy < 2; ++dy)
    {
        for (int32_t dx = 0; dx < 2; ++dx)
        {
            PlanetPatch patch{};
            patch.x = x * 2 + dx;
            patch.y = y * 2 + dy;
            patch.level = level;
            
            // Simple bounding sphere calculation
            patch.center[0] = 0.0f;
            patch.center[1] = 0.0f; 
            patch.center[2] = 0.0f;
            patch.radius = radius_;
            patch.materialIndex = 0;
            patch.vertexOffset = 0;
            
            patches.push_back(patch);
        }
    }
    
    return patches;
}

uint32_t PlanetQuadtree::getMaxLevel() const
{
    return maxLevel_;
}

} // namespace starforge::planets