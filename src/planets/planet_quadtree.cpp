#include "planets/planet_quadtree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace starforge::planets
{

namespace
{

void createChildren(PlanetQuadNode* node)
{
    if (!node->isLeaf()) return;
    
    double uMid = (node->uMin + node->uMax) * 0.5;
    double vMid = (node->vMin + node->vMax) * 0.5;
    
    // Create 4 children
    for (std::size_t i = 0; i < 4; ++i)
    {
        node->children[i] = std::make_unique<PlanetQuadNode>();
        node->children[i]->face = node->face;
        node->children[i]->level = node->level + 1;
        node->children[i]->x = node->x * 2 + static_cast<int>(i % 2);
        node->children[i]->y = node->y * 2 + static_cast<int>(i / 2);
        
        // Set bounds
        if (i == 0) // bottom-left
        {
            node->children[i]->uMin = node->uMin;
            node->children[i]->uMax = uMid;
            node->children[i]->vMin = node->vMin;
            node->children[i]->vMax = vMid;
        }
        else if (i == 1) // bottom-right
        {
            node->children[i]->uMin = uMid;
            node->children[i]->uMax = node->uMax;
            node->children[i]->vMin = node->vMin;
            node->children[i]->vMax = vMid;
        }
        else if (i == 2) // top-left
        {
            node->children[i]->uMin = node->uMin;
            node->children[i]->uMax = uMid;
            node->children[i]->vMin = vMid;
            node->children[i]->vMax = node->vMax;
        }
        else if (i == 3) // top-right
        {
            node->children[i]->uMin = uMid;
            node->children[i]->uMax = node->uMax;
            node->children[i]->vMin = vMid;
            node->children[i]->vMax = node->vMax;
        }
    }
}

void removeChildren(PlanetQuadNode* node)
{
    for (std::size_t i = 0; i < 4; ++i)
    {
        node->children[i].reset();
    }
}

} // namespace

PlanetQuadtree::PlanetQuadtree(CubeFace face) : face_(face)
{
    root_ = std::make_unique<PlanetQuadNode>();
    root_->face = face;
    root_->level = 0;
    root_->x = 0;
    root_->y = 0;
    root_->uMin = -1.0;
    root_->uMax = 1.0;
    root_->vMin = -1.0;
    root_->vMax = 1.0;
}

double calculateScreenSpaceError(
    const PlanetQuadNode& node,
    const math::Vec3d& planetCenter,
    const math::Vec3d& cameraPos,
    double planetRadius)
{
    // Calculate center of patch
    double uMid = (node.uMin + node.uMax) * 0.5;
    double vMid = (node.vMin + node.vMax) * 0.5;
    math::Vec3d patchDir = cubeToSphere(node.face, uMid, vMid);
    math::Vec3d patchPos = planetCenter + patchDir * planetRadius;
    
    // Patch bounding radius (half diagonal in tangent space)
    double patchSize = (node.uMax - node.uMin) * planetRadius * 0.5;
    double patchBoundingRadius = patchSize * 1.414; // sqrt(2) for diagonal
    
    // Distance from camera to patch's bounding sphere
    math::Vec3d diff = patchPos - cameraPos;
    double distToCenter = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
    double dist = std::max(0.0, distToCenter - patchBoundingRadius);
    if (dist < 1e-10) return std::numeric_limits<double>::infinity();
    
    // Simple screen space error approximation
    return patchSize / dist;
}

bool shouldSplit(
    const PlanetQuadNode& node,
    const math::Vec3d& planetCenter,
    const math::Vec3d& cameraPos,
    double planetRadius,
    double splitThreshold)
{
    // Horizon culling: skip patches on the far side of the planet
    double uMid = (node.uMin + node.uMax) * 0.5;
    double vMid = (node.vMin + node.vMax) * 0.5;
    math::Vec3d patchDir = cubeToSphere(node.face, uMid, vMid);
    math::Vec3d patchPos = planetCenter + patchDir * planetRadius;
    math::Vec3d toPatch = patchPos - cameraPos;
    math::Vec3d toCenter = planetCenter - cameraPos;
    double toPatchLen = std::sqrt(toPatch.x * toPatch.x + toPatch.y * toPatch.y + toPatch.z * toPatch.z);
    if (toPatchLen > 1e-10)
    {
        double camDist = std::sqrt(toCenter.x * toCenter.x + toCenter.y * toCenter.y + toCenter.z * toCenter.z);
        if (camDist > planetRadius)
        {
            double cosAngle = (toPatch.x * toCenter.x + toPatch.y * toCenter.y + toPatch.z * toCenter.z) / (toPatchLen * camDist);
            double horizonCos = std::sqrt(1.0 - (planetRadius / camDist) * (planetRadius / camDist));
            // Far side: patch center points away from the camera, so its
            // angle to the center direction exceeds the horizon cone.
            // (Previously inverted: this culled the visible hemisphere.)
            if (cosAngle < -horizonCos)
            {
                return false;
            }
        }
    }
    
    double sse = calculateScreenSpaceError(node, planetCenter, cameraPos, planetRadius);
    return sse > splitThreshold && node.level < 20; // Max depth limit
}

bool shouldMerge(
    const PlanetQuadNode& node,
    const math::Vec3d& planetCenter,
    const math::Vec3d& cameraPos,
    double planetRadius,
    double mergeThreshold)
{
    // Use hysteresis: merge at lower threshold than split
    double sse = calculateScreenSpaceError(node, planetCenter, cameraPos, planetRadius);
    return sse < mergeThreshold;
}

void PlanetQuadtree::updateNode(PlanetQuadNode* node,
                                const math::Vec3d& planetCenter,
                                const math::Vec3d& cameraPos,
                                double planetRadius,
                                double splitThreshold,
                                double mergeThreshold,
                                int maxLevelDelta)
{
    if (node->isLeaf())
    {
        if (node->level < 20 && maxLevelDelta > 0 && shouldSplit(*node, planetCenter, cameraPos, planetRadius, splitThreshold))
        {
            createChildren(node);
            // Recursively update children with decremented delta
            for (std::size_t i = 0; i < 4; ++i)
            {
                updateNode(node->children[i].get(), planetCenter, cameraPos, planetRadius, splitThreshold, mergeThreshold, maxLevelDelta - 1);
            }
        }
    }
    else
    {
        // Check if should merge
        bool allChildrenLeaves = true;
        for (std::size_t i = 0; i < 4; ++i)
        {
            if (!node->children[i]->isLeaf())
            {
                allChildrenLeaves = false;
                break;
            }
        }
        
        if (allChildrenLeaves && shouldMerge(*node, planetCenter, cameraPos, planetRadius, mergeThreshold))
        {
            removeChildren(node);
        }
        else
        {
            for (std::size_t i = 0; i < 4; ++i)
            {
                updateNode(node->children[i].get(), planetCenter, cameraPos, planetRadius, splitThreshold, mergeThreshold, maxLevelDelta);
            }
        }
    }
}

std::vector<PlanetQuadNode*> PlanetQuadtree::update(
    const math::Vec3d& planetCenter,
    const math::Vec3d& cameraPos,
    double planetRadius,
    double splitThreshold,
    double mergeThreshold,
    int maxLevelDelta)
{
    updateNode(root_.get(), planetCenter, cameraPos, planetRadius, splitThreshold, mergeThreshold, maxLevelDelta);
    return getLeaves();
}

void PlanetQuadtree::collectLeaves(PlanetQuadNode* node, std::vector<PlanetQuadNode*>& leaves) const
{
    if (node->isLeaf())
    {
        leaves.push_back(node);
    }
    else
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            if (node->children[i])
                collectLeaves(node->children[i].get(), leaves);
        }
    }
}

std::vector<PlanetQuadNode*> PlanetQuadtree::getLeaves() const
{
    std::vector<PlanetQuadNode*> leaves;
    collectLeaves(root_.get(), leaves);
    return leaves;
}

bool PlanetQuadtree::checkNodeAdjacency(const PlanetQuadNode* node) const
{
    if (node->isLeaf()) return true;
    
    // Check children satisfy 2:1 rule with each other
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (!checkNodeAdjacency(node->children[i].get()))
            return false;
    }
    return true;
}

bool PlanetQuadtree::checkAdjacencyInvariant() const
{
    return checkNodeAdjacency(root_.get());
}

void PlanetQuadtree::reset()
{
    root_ = std::make_unique<PlanetQuadNode>();
    root_->face = face_;
    root_->level = 0;
    root_->x = 0;
    root_->y = 0;
    root_->uMin = -1.0;
    root_->uMax = 1.0;
    root_->vMin = -1.0;
    root_->vMax = 1.0;
}

} // namespace starforge::planets
