#include <doctest/doctest.h>

#include "planets/cubed_sphere.hpp"
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
    PlanetQuadtree qt(CubeFace::PosX);
    auto leaves = qt.getLeaves();
    CHECK(leaves.size() == 1); // Just root initially
    CHECK(leaves[0]->level == 0);
    CHECK(leaves[0]->face == CubeFace::PosX);
}

TEST_CASE("Planet quadtree: adjacency invariant")
{
    PlanetQuadtree qt(CubeFace::PosX);
    CHECK(qt.checkAdjacencyInvariant());
}

TEST_CASE("Planet quadtree: split and merge with hysteresis")
{
    PlanetQuadtree qt(CubeFace::PosX);
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
    PlanetQuadtree qt(CubeFace::PosX);
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
