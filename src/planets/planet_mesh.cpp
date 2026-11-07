#include "planets/planet_mesh.hpp"
#include "planets/cubed_sphere.hpp"

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
    const std::size_t vertexCount = static_cast<std::size_t>(vertsPerSide) * vertsPerSide;
    const std::size_t indexCount = static_cast<std::size_t>(n) * n * 6;

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

} // namespace starforge::planets
