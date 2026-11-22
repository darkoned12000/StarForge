#include "planets/planet_mesh.hpp"
#include "planets/cubed_sphere.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::planets
{

PlanetPatchMesh generatePatchMesh(
    const PlanetPatch& patch, std::uint32_t subdivisions, double radius)
{
    PlanetPatchMesh mesh;
    if (subdivisions == 0)
    {
        return mesh;
    }

    const std::uint32_t n = subdivisions;
    const std::uint32_t vertsPerSide = n + 1;
    const std::size_t vertexCount = static_cast<std::size_t>(vertsPerSide) * static_cast<std::size_t>(vertsPerSide);
    const std::size_t indexCount = static_cast<std::size_t>(n) * static_cast<std::size_t>(n) * 6u;

    mesh.positions.resize(vertexCount);
    mesh.normals.resize(vertexCount);
    mesh.uvs.resize(vertexCount);
    mesh.indices.resize(indexCount);

    const double uMin = patch.uMin;
    const double uMax = patch.uMax;
    const double vMin = patch.vMin;
    const double vMax = patch.vMax;
    const double uRange = uMax - uMin;
    const double vRange = vMax - vMin;

    std::size_t vertIdx = 0;
    for (std::uint32_t j = 0; j < vertsPerSide; ++j)
    {
        const double v = vMin + vRange * (static_cast<double>(j) / static_cast<double>(n));
        for (std::uint32_t i = 0; i < vertsPerSide; ++i)
        {
            const double u = uMin + uRange * (static_cast<double>(i) / static_cast<double>(n));
            const math::Vec3d dir = cubeToSphere(patch.face, u, v);

            const math::Vec3f norm{
                static_cast<float>(dir.x),
                static_cast<float>(dir.y),
                static_cast<float>(dir.z)
            };

            // RTC: subtract the (double) patch center in double precision,
            // then store the small offset as float.
            const math::Vec3d rel = dir * radius - patch.center;
            mesh.positions[vertIdx] = math::Vec3f{
                static_cast<float>(rel.x),
                static_cast<float>(rel.y),
                static_cast<float>(rel.z)
            };
            mesh.normals[vertIdx] = norm;
            mesh.uvs[vertIdx] = math::Vec2f{static_cast<float>(u), static_cast<float>(v)};
            ++vertIdx;
        }
    }

    // Two triangles per quad. Winding is chosen so triangle normals point
    // away from the planet center (verified by test).
    std::size_t idx = 0;
    for (std::uint32_t j = 0; j < n; ++j)
    {
        for (std::uint32_t i = 0; i < n; ++i)
        {
            const std::uint32_t a = j * vertsPerSide + i;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + vertsPerSide;
            const std::uint32_t d = c + 1;

            mesh.indices[idx++] = a;
            mesh.indices[idx++] = c;
            mesh.indices[idx++] = b;
            mesh.indices[idx++] = b;
            mesh.indices[idx++] = c;
            mesh.indices[idx++] = d;
        }
    }

    return mesh;
}

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
    const CubePatchUvBounds bounds = cubePatchUvBounds(x, y, level);
    patch.uMin = bounds.uMin;
    patch.uMax = bounds.uMax;
    patch.vMin = bounds.vMin;
    patch.vMax = bounds.vMax;

    // Bounding sphere: sample a regular grid over the patch UV to get a
    // conservative bound for the cubed-sphere surface. The patch center is
    // the spherical projection of the UV center, which keeps the center on
    // the planet surface and stable across LOD levels.
    const double uMid = 0.5 * (patch.uMin + patch.uMax);
    const double vMid = 0.5 * (patch.vMin + patch.vMax);
    patch.center = radius_ * cubeToSphere(face, uMid, vMid);

    double maxDistSq = 0.0;
    constexpr int kSamples = 5;
    for (int iy = 0; iy < kSamples; ++iy)
    {
        const double tV = static_cast<double>(iy) / static_cast<double>(kSamples - 1);
        const double v = patch.vMin + (patch.vMax - patch.vMin) * tV;
        for (int ix = 0; ix < kSamples; ++ix)
        {
            const double tU = static_cast<double>(ix) / static_cast<double>(kSamples - 1);
            const double u = patch.uMin + (patch.uMax - patch.uMin) * tU;
            const math::Vec3d p = radius_ * cubeToSphere(face, u, v);
            const math::Vec3d d = p - patch.center;
            const double distSq = d.x * d.x + d.y * d.y + d.z * d.z;
            maxDistSq = std::max(maxDistSq, distSq);
        }
    }

    // Small margin for curvature between samples.
    patch.boundingRadius = std::sqrt(maxDistSq) * 1.001;
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
