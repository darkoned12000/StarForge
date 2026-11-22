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

    for (std::size_t i = 0; i < 4; ++i)
    {
        auto child = std::make_unique<PlanetQuadNode>();
        child->face = node->face;
        child->level = node->level + 1;
        child->x = node->x * 2 + static_cast<int>(i % 2);
        child->y = node->y * 2 + static_cast<int>(i / 2);

        const CubePatchUvBounds bounds = cubePatchUvBounds(child->x, child->y, static_cast<std::uint32_t>(child->level));
        child->uMin = bounds.uMin;
        child->uMax = bounds.uMax;
        child->vMin = bounds.vMin;
        child->vMax = bounds.vMax;

        node->children[i] = std::move(child);
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
    const CubePatchUvBounds bounds = cubePatchUvBounds(0, 0, 0);
    root_->uMin = bounds.uMin;
    root_->uMax = bounds.uMax;
    root_->vMin = bounds.vMin;
    root_->vMax = bounds.vMax;
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
            // Horizon culling: the horizon plane is orthogonal to the
            // camera direction u at distance R^2 / camDist from the
            // planet center. The patch's bounding sphere lies entirely
            // behind it when (P - C).u < R^2/camDist - r_bound.
            // (Both previous forms were wrong: this is the plane test.)
            const double ux = -toCenter.x / camDist;
            const double uy = -toCenter.y / camDist;
            const double uz = -toCenter.z / camDist;
            const double horizonPlaneDist = (planetRadius * planetRadius) / camDist;
            const double patchProj = (patchPos.x - planetCenter.x) * ux +
                                     (patchPos.y - planetCenter.y) * uy +
                                     (patchPos.z - planetCenter.z) * uz;
            const double patchSize = (node.uMax - node.uMin) * planetRadius * 0.5;
            const double patchBoundingRadius = patchSize * 1.41421356;
            if (patchProj < horizonPlaneDist - patchBoundingRadius)
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
    enforceBalance();
    return getLeaves();
}

struct PlanetQuadtree::ParallelLeafJob
{
    PlanetQuadtree* tree = nullptr;
    PlanetQuadNode* node = nullptr;
    const math::Vec3d* planetCenter = nullptr;
    const math::Vec3d* cameraPos = nullptr;
    double planetRadius = 0.0;
    double splitThreshold = 0.0;
    double mergeThreshold = 0.0;
    int maxLevelDelta = 1;
};

void PlanetQuadtree::parallelLeafTrampoline(void* userData)
{
    auto* job = static_cast<ParallelLeafJob*>(userData);
    job->tree->updateNode(job->node, *job->planetCenter, *job->cameraPos,
                          job->planetRadius, job->splitThreshold,
                          job->mergeThreshold, job->maxLevelDelta);
}

std::vector<PlanetQuadNode*> PlanetQuadtree::updateParallel(
    const math::Vec3d& planetCenter,
    const math::Vec3d& cameraPos,
    double planetRadius,
    double splitThreshold,
    double mergeThreshold,
    jobs::JobScheduler& scheduler,
    int maxLevelDelta)
{
    std::vector<PlanetQuadNode*> leaves = getLeaves();
    if (leaves.size() < 2 || scheduler.threadCount() <= 1)
    {
        updateNode(root_.get(), planetCenter, cameraPos, planetRadius, splitThreshold, mergeThreshold, maxLevelDelta);
        enforceBalance();
        return getLeaves();
    }

    std::vector<ParallelLeafJob> jobs;
    jobs.reserve(leaves.size());
    std::vector<jobs::TaskHandle> handles;
    handles.reserve(leaves.size());

    for (PlanetQuadNode* leaf : leaves)
    {
        jobs.push_back(ParallelLeafJob{
            this, leaf, &planetCenter, &cameraPos,
            planetRadius, splitThreshold, mergeThreshold, maxLevelDelta,
        });
        handles.push_back(scheduler.submit(&parallelLeafTrampoline, &jobs.back()));
    }

    for (const auto& handle : handles)
    {
        scheduler.wait(handle);
    }

    // Merge pass (sequential, bottom-up): parallel jobs only see leaves,
    // so pruning must happen here for retreat-to-orbit to work.
    mergePass(root_.get(), planetCenter, cameraPos, planetRadius, mergeThreshold);
    enforceBalance();

    return getLeaves();
}

void PlanetQuadtree::mergePass(PlanetQuadNode* node,
                               const math::Vec3d& planetCenter,
                               const math::Vec3d& cameraPos,
                               double planetRadius,
                               double mergeThreshold)
{
    if (node->isLeaf())
    {
        return;
    }

    for (std::size_t i = 0; i < 4; ++i)
    {
        mergePass(node->children[i].get(), planetCenter, cameraPos, planetRadius, mergeThreshold);
    }

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
}

void PlanetQuadtree::enforceBalance()
{
    // In-face 2:1 balance. Cross-face neighbors are ignored because this
    // quadtree owns a single cube face.
    constexpr int kMaxIterations = 256;
    for (int iter = 0; iter < kMaxIterations; ++iter)
    {
        bool changed = false;
        std::vector<PlanetQuadNode*> leaves;
        collectLeaves(root_.get(), leaves);

        for (PlanetQuadNode* leaf : leaves)
        {
            const int level = leaf->level;
            if (level <= 1)
            {
                continue;
            }

            const int extent = 1 << level;
            static constexpr int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& dir : kDirs)
            {
                const int nx = leaf->x + dir[0];
                const int ny = leaf->y + dir[1];
                if (nx < 0 || ny < 0 || nx >= extent || ny >= extent)
                {
                    continue; // Cross-face edge: not handled here.
                }

                PlanetQuadNode* node = root_.get();
                while (node != nullptr)
                {
                    if (node->isLeaf())
                    {
                        if (node->level < level - 1 && node->level < 20)
                        {
                            createChildren(node);
                            changed = true;
                            // Fall through to descend into the new child
                            // that covers (nx, ny).
                        }
                        else
                        {
                            break;
                        }
                    }

                    const int childLevel = node->level + 1;
                    const int shift = level - childLevel;
                    if (shift < 0)
                    {
                        break;
                    }
                    const int bx = (nx >> shift) & 1;
                    const int by = (ny >> shift) & 1;
                    const std::size_t childIndex = static_cast<std::size_t>(by * 2 + bx);
                    node = node->children[childIndex].get();
                }
            }
        }

        if (!changed)
        {
            break;
        }
    }
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

namespace
{

struct LeafDepthRange
{
    int minDepth = 0;
    int maxDepth = 0;
};

LeafDepthRange leafDepthRange(const PlanetQuadNode* node)
{
    if (node->isLeaf())
    {
        return {node->level, node->level};
    }

    LeafDepthRange range = leafDepthRange(node->children[0].get());
    for (std::size_t i = 1; i < 4; ++i)
    {
        const LeafDepthRange childRange = leafDepthRange(node->children[i].get());
        range.minDepth = std::min(range.minDepth, childRange.minDepth);
        range.maxDepth = std::max(range.maxDepth, childRange.maxDepth);
    }
    return range;
}

bool edgeAdjacentBalanced(const LeafDepthRange& a, const LeafDepthRange& b)
{
    // 2:1 balance: adjacent leaf regions must not differ by more than one
    // LOD level. This range check is necessary but not sufficient (it can
    // miss border-local violations inside overlapping ranges), but it never
    // rejects a valid tree and catches gross imbalance the previous
    // same-child-level check missed entirely.
    return a.minDepth <= b.maxDepth + 1 && b.minDepth <= a.maxDepth + 1;
}

} // namespace

bool PlanetQuadtree::checkNodeAdjacency(const PlanetQuadNode* node) const
{
    if (node->isLeaf()) return true;

    // Check edge-adjacent sibling subtrees (0:BL,1:BR,2:TL,3:TR).
    const LeafDepthRange r0 = leafDepthRange(node->children[0].get());
    const LeafDepthRange r1 = leafDepthRange(node->children[1].get());
    const LeafDepthRange r2 = leafDepthRange(node->children[2].get());
    const LeafDepthRange r3 = leafDepthRange(node->children[3].get());

    if (!edgeAdjacentBalanced(r0, r1) || !edgeAdjacentBalanced(r2, r3) ||
        !edgeAdjacentBalanced(r0, r2) || !edgeAdjacentBalanced(r1, r3))
    {
        return false;
    }

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
    const CubePatchUvBounds bounds = cubePatchUvBounds(0, 0, 0);
    root_->uMin = bounds.uMin;
    root_->uMax = bounds.uMax;
    root_->vMin = bounds.vMin;
    root_->vMax = bounds.vMax;
}

} // namespace starforge::planets
