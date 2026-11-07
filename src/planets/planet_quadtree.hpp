#pragma once

#include "jobs/job_scheduler.hpp"
#include "planets/cubed_sphere.hpp"
#include "spatial/world_position.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace starforge::planets
{

// A node in the planet quadtree.
struct PlanetQuadNode
{
    CubeFace face = CubeFace::PositiveX;
    int level = 0;           // LOD level (0 = root)
    int x = 0;               // Node index in quadtree
    int y = 0;
    
    // Bounds in normalized tangent space [-1, 1]^2
    double uMin = -1.0;
    double uMax = 1.0;
    double vMin = -1.0;
    double vMax = 1.0;
    
    // Children (nullptr if leaf)
    std::array<std::unique_ptr<PlanetQuadNode>, 4> children;
    
    bool isLeaf() const noexcept { return children[0] == nullptr; }
};

// Quadtree for a single cube face.
class PlanetQuadtree
{
public:
    PlanetQuadtree(CubeFace face);
    
    // Update quadtree based on camera position relative to planet center.
    // Returns list of leaf nodes that should be rendered.
    std::vector<PlanetQuadNode*> update(
        const math::Vec3d& planetCenter,
        const math::Vec3d& cameraPos,
        double planetRadius,
        double splitThreshold,
        double mergeThreshold,
        int maxLevelDelta = 1);

    // Parallel update (M7.1): snapshots the current leaves and runs one
    // job per leaf subtree on `scheduler`. Subtrees are disjoint, so
    // results are bit-identical to update() regardless of job order.
    std::vector<PlanetQuadNode*> updateParallel(
        const math::Vec3d& planetCenter,
        const math::Vec3d& cameraPos,
        double planetRadius,
        double splitThreshold,
        double mergeThreshold,
        jobs::JobScheduler& scheduler,
        int maxLevelDelta = 1);

    // Get all leaf nodes.
    std::vector<PlanetQuadNode*> getLeaves() const;
    
    // Check adjacency invariant (neighbor LOD difference <= 1).
    bool checkAdjacencyInvariant() const;
    
    void reset();
    
private:
    void updateNode(PlanetQuadNode* node,
                    const math::Vec3d& planetCenter,
                    const math::Vec3d& cameraPos,
                    double planetRadius,
                    double splitThreshold,
                    double mergeThreshold,
                    int maxLevelDelta);
    
    void collectLeaves(PlanetQuadNode* node, std::vector<PlanetQuadNode*>& leaves) const;
    bool checkNodeAdjacency(const PlanetQuadNode* node) const;

    // Bottom-up merge pass used by updateParallel after the split pass.
    void mergePass(PlanetQuadNode* node,
                   const math::Vec3d& planetCenter,
                   const math::Vec3d& cameraPos,
                   double planetRadius,
                   double mergeThreshold);

    struct ParallelLeafJob;
    static void parallelLeafTrampoline(void* userData);
    
    std::unique_ptr<PlanetQuadNode> root_;
    CubeFace face_;
};

// Calculate screen-space error for a node.
double calculateScreenSpaceError(
    const PlanetQuadNode& node,
    const math::Vec3d& planetCenter,
    const math::Vec3d& cameraPos,
    double planetRadius);

// Check if node should split based on distance and SSE.
bool shouldSplit(
    const PlanetQuadNode& node,
    const math::Vec3d& planetCenter,
    const math::Vec3d& cameraPos,
    double planetRadius,
    double splitThreshold);

// Check if node should merge (with hysteresis).
bool shouldMerge(
    const PlanetQuadNode& node,
    const math::Vec3d& planetCenter,
    const math::Vec3d& cameraPos,
    double planetRadius,
    double mergeThreshold);

} // namespace starforge::planets
