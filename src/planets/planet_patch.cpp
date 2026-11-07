#include "planets/planet_patch.hpp"
#include "planets/cubed_sphere.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::planets
{

namespace
{

double vecLength(const math::Vec3d& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

} // namespace

PlanetPatchGenerator::PlanetPatchGenerator(double radius, double maxError)
    : radius_(radius), maxError_(maxError)
{
    // Derive max LOD from the error tolerance: patch size halves each
    // level, so stop subdividing once radius / 2^level <= maxError.
    // Guard ratio <= 1: log2 of a negative/zero ratio is undefined and a
    // coarse tolerance (maxError > radius) must yield level 1, not max.
    if (maxError_ > 0.0 && radius_ > 0.0)
    {
        const double ratio = radius_ / maxError_;
        if (ratio <= 1.0)
        {
            maxLevel_ = 1;
        }
        else
        {
            const auto level = static_cast<std::uint32_t>(std::ceil(std::log2(ratio)));
            maxLevel_ = std::clamp(level, 1u, 20u);
        }
    }
    else
    {
        maxLevel_ = 10;
    }
}

PlanetPatch PlanetPatchGenerator::createRootPatch(CubeFace face) const
{
    return createPatch(face, 0, 0, 0);
}

PlanetPatch PlanetPatchGenerator::createPatch(CubeFace face, std::int32_t x, std::int32_t y, std::uint32_t level) const
{
    PlanetPatch patch;
    patch.face = face;
    patch.x = x;
    patch.y = y;
    patch.level = level;

    // Map integer grid (x, y) at `level` to normalized cube coords [-1, 1].
    // Level is capped at 20 by maxLevel_, so 1u << level cannot overflow.
    const double span = 2.0 / static_cast<double>(1u << level);
    patch.uMin = -1.0 + static_cast<double>(x) * span;
    patch.uMax = patch.uMin + span;
    patch.vMin = -1.0 + static_cast<double>(y) * span;
    patch.vMax = patch.vMin + span;

    // Analytical bounding sphere: evaluate 4 patch corners and midpoint.
    const double uMid = 0.5 * (patch.uMin + patch.uMax);
    const double vMid = 0.5 * (patch.vMin + patch.vMax);

    const math::Vec3d c0 = radius_ * cubeToSphere(face, patch.uMin, patch.vMin);
    const math::Vec3d c1 = radius_ * cubeToSphere(face, patch.uMax, patch.vMin);
    const math::Vec3d c2 = radius_ * cubeToSphere(face, patch.uMin, patch.vMax);
    const math::Vec3d c3 = radius_ * cubeToSphere(face, patch.uMax, patch.vMax);
    const math::Vec3d mid = radius_ * cubeToSphere(face, uMid, vMid);

    patch.center = 0.2 * (c0 + c1 + c2 + c3 + mid);

    const double d0 = vecLength(c0 - patch.center);
    const double d1 = vecLength(c1 - patch.center);
    const double d2 = vecLength(c2 - patch.center);
    const double d3 = vecLength(c3 - patch.center);
    const double dm = vecLength(mid - patch.center);

    patch.boundingRadius = std::max({d0, d1, d2, d3, dm});
    return patch;
}

std::vector<PlanetPatch> PlanetPatchGenerator::subdivide(const PlanetPatch& parent) const
{
    if (parent.level >= maxLevel_)
    {
        return {};
    }

    std::vector<PlanetPatch> children;
    children.reserve(4);

    const std::uint32_t nextLevel = parent.level + 1;
    for (std::int32_t dy = 0; dy < 2; ++dy)
    {
        for (std::int32_t dx = 0; dx < 2; ++dx)
        {
            children.push_back(createPatch(parent.face, parent.x * 2 + dx, parent.y * 2 + dy, nextLevel));
        }
    }

    return children;
}

} // namespace starforge::planets
