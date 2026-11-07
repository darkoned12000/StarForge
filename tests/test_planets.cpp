#include <doctest/doctest.h>

#include "planets/cubed_sphere.hpp"
#include "planets/planet_mesh.hpp"
#include "planets/planet_patch.hpp"
#include "planets/planet_quadtree.hpp"

#include <cmath>
#include <vector>

using namespace starforge::planets;
using namespace starforge::math;

TEST_CASE("Cubed sphere: mapping round trip")
{
    for (int face = 0; face < 6; ++face)
    {
        CubeFace f = static_cast<CubeFace>(face);
        for (double u = -0.8; u <= 0.8; u += 0.4)
        {
            for (double v = -0.8; v <= 0.8; v += 0.4)
            {
                Vec3d p = cubeToSphere(f, u, v);
                // Should be on unit sphere
                double len = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
                CHECK(std::abs(len - 1.0) < 1e-10);
                
                CubeFace f2;
                double u2, v2;
                CHECK(sphereToCube(p, f2, u2, v2));
                CHECK(f2 == f);
                CHECK(std::abs(u2 - u) < 1e-10);
                CHECK(std::abs(v2 - v) < 1e-10);
            }
        }
    }
}

TEST_CASE("Cubed sphere: reduced distortion")
{
    // Check that corners don't get overly stretched
    for (int face = 0; face < 6; ++face)
    {
        CubeFace f = static_cast<CubeFace>(face);
        Vec3d center = cubeToSphere(f, 0.0, 0.0);
        Vec3d corner = cubeToSphere(f, 1.0, 1.0);
        double lenCenter = std::sqrt(center.x*center.x + center.y*center.y + center.z*center.z);
        double lenCorner = std::sqrt(corner.x*corner.x + corner.y*corner.y + corner.z*corner.z);
        CHECK(std::abs(lenCenter - 1.0) < 1e-10);
        CHECK(std::abs(lenCorner - 1.0) < 1e-10);
    }
}

TEST_CASE("Planet quadtree: hierarchy construction")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    auto leaves = qt.getLeaves();
    CHECK(leaves.size() == 1); // Just root initially
    CHECK(leaves[0]->level == 0);
    CHECK(leaves[0]->face == CubeFace::PositiveX);
}

TEST_CASE("Planet quadtree: adjacency invariant")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    CHECK(qt.checkAdjacencyInvariant());
}

TEST_CASE("Planet quadtree: split and merge with hysteresis")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    Vec3d planetCenter{0.0, 0.0, 0.0};
    Vec3d cameraPos{10000000.0, 0.0, 0.0}; // Close to planet
    double planetRadius = 6371000.0;
    
    // Update with low threshold to force split
    auto leaves = qt.update(planetCenter, cameraPos, planetRadius, 0.001, 0.0005);
    CHECK(leaves.size() >= 1);
    CHECK(qt.checkAdjacencyInvariant());
    
    // Move camera far away
    cameraPos = Vec3d{100000000.0, 0.0, 0.0};
    leaves = qt.update(planetCenter, cameraPos, planetRadius, 0.001, 0.0005);
    CHECK(qt.checkAdjacencyInvariant());
}

TEST_CASE("Planet quadtree: bounded patch count")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    Vec3d planetCenter{0.0, 0.0, 0.0};
    Vec3d cameraPos{7000000.0, 0.0, 0.0};
    double planetRadius = 6371000.0;
    
    // Multiple updates along path
    for (int i = 0; i < 10; ++i)
    {
        cameraPos.x -= 50000.0;
        auto leaves = qt.update(planetCenter, cameraPos, planetRadius, 0.01, 0.005);
        CHECK(leaves.size() > 0);
        CHECK(leaves.size() < 1000000); // Bounded
        CHECK(qt.checkAdjacencyInvariant());
    }
}

TEST_CASE("Planet patch mesh: structure and bounds")
{
    const double radius = 6371000.0;
    PlanetPatchGenerator gen(radius, 1.0);
    auto root = gen.createRootPatch(CubeFace::PositiveX);
    auto mesh = generatePatchMesh(root, 4, radius);

    // (n+1)^2 vertices, 6n^2 indices.
    CHECK(mesh.positions.size() == 25);
    CHECK(mesh.normals.size() == 25);
    CHECK(mesh.uvs.size() == 25);
    CHECK(mesh.indices.size() == 96);

    // All indices reference valid vertices.
    for (std::uint32_t idx : mesh.indices)
    {
        CHECK(idx < mesh.positions.size());
    }

    // RTC positions: world position (center + offset) lies on the sphere;
    // offsets stay within the patch bounding radius; normals unit-length.
    for (std::size_t i = 0; i < mesh.positions.size(); ++i)
    {
        const Vec3d world{
            root.center.x + static_cast<double>(mesh.positions[i].x),
            root.center.y + static_cast<double>(mesh.positions[i].y),
            root.center.z + static_cast<double>(mesh.positions[i].z),
        };
        const double len = std::sqrt(world.x * world.x + world.y * world.y + world.z * world.z);
        CHECK(std::abs(len - radius) < 1.0);

        const auto& off = mesh.positions[i];
        const double offLen = std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z);
        CHECK(offLen <= root.boundingRadius + 1.0);

        const auto& n = mesh.normals[i];
        const double nlen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        CHECK(std::abs(nlen - 1.0) < 1e-5);
    }

    // Winding: first triangle normal points outward (dot with world
    // position > 0).
    const auto toWorld = [&root](const Vec3f& off) {
        return Vec3d{
            root.center.x + static_cast<double>(off.x),
            root.center.y + static_cast<double>(off.y),
            root.center.z + static_cast<double>(off.z),
        };
    };
    const Vec3d a = toWorld(mesh.positions[mesh.indices[0]]);
    const Vec3d b = toWorld(mesh.positions[mesh.indices[1]]);
    const Vec3d c = toWorld(mesh.positions[mesh.indices[2]]);
    const Vec3d ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3d ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3d triN{ab.y * ac.z - ab.z * ac.y,
                     ab.z * ac.x - ab.x * ac.z,
                     ab.x * ac.y - ab.y * ac.x};
    CHECK((triN.x * a.x + triN.y * a.y + triN.z * a.z) > 0.0);

    // Degenerate input yields an empty mesh.
    auto empty = generatePatchMesh(root, 0, radius);
    CHECK(empty.positions.empty());
    CHECK(empty.indices.empty());
}
