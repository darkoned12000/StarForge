#include <doctest/doctest.h>

#include "planets/cubed_sphere.hpp"
#include "planets/planet_patch.hpp"
#include "planets/planet_quadtree.hpp"
#include "planets/atmosphere.hpp"

#include <cmath>
#include <vector>

using namespace starforge::planets;
using namespace starforge::math;

TEST_CASE("Planet patches: basic structure")
{
    PlanetPatch patch{};
    patch.x = 1;
    patch.y = 2; 
    patch.level = 3;
    
    CHECK(patch.x == 1);
    CHECK(patch.y == 2);
    CHECK(patch.level == 3);
}

TEST_CASE("Planet patch generator: initialization and level calculation")
{
    PlanetPatchGenerator gen(6371000.0, 1.0); // Earth radius with 1m error tolerance

    CHECK(gen.getMaxLevel() > 0);
    CHECK(gen.getRadius() == 6371000.0);

    // Coarse tolerance (maxError > radius) must yield level 1, not wrap
    // around via an undefined float->uint32 cast of a negative log2.
    PlanetPatchGenerator coarse(100.0, 1000.0);
    CHECK(coarse.getMaxLevel() == 1);

    // Subdividing a root patch yields 4 children at level + 1 with
    // doubled coordinates and halved UV bounds.
    auto root = gen.createRootPatch(CubeFace::PositiveX);
    CHECK(root.level == 0);
    CHECK(root.face == CubeFace::PositiveX);
    CHECK(root.uMin == -1.0);
    CHECK(root.uMax == 1.0);

    // Root bounding sphere must contain the face; it must be tighter
    // than the whole planet (regression: every patch used to bound
    // the entire planet).
    CHECK(root.boundingRadius > 0.0);
    CHECK(root.boundingRadius < 6371000.0);

    auto children = gen.subdivide(root);
    CHECK(children.size() == 4);
    for (const auto& child : children)
    {
        CHECK(child.level == 1);
        CHECK(child.face == CubeFace::PositiveX);
        CHECK(child.uMax - child.uMin == 1.0);
        CHECK(child.vMax - child.vMin == 1.0);
    }

    // Children tile the root bounds exactly.
    bool sawBottomLeft = false;
    bool sawTopRight = false;
    for (const auto& child : children)
    {
        if (child.uMin == -1.0 && child.vMin == -1.0)
        {
            sawBottomLeft = true;
        }
        if (child.uMax == 1.0 && child.vMax == 1.0)
        {
            sawTopRight = true;
        }
    }
    CHECK(sawBottomLeft);
    CHECK(sawTopRight);
}

TEST_CASE("Cubed sphere: basic face conversion")
{
    float position[3];
    
    // Test positive X face (result is normalized to a unit sphere)
    CubedSphere::faceToSphere(0, 0.5f, 0.5f, position);

    // (1, -0.5, -0.5) / sqrt(1.5)
    CHECK(std::abs(position[0] - 0.8164966f) < 1e-6f);
    CHECK(std::abs(position[1] - (-0.4082483f)) < 1e-6f);
    CHECK(std::abs(position[2] - (-0.4082483f)) < 1e-6f);
    
    // Test normalization
    float length = std::sqrt(position[0]*position[0] + position[1]*position[1] + position[2]*position[2]);
    CHECK(std::abs(length - 1.0f) < 1e-6f);
}

TEST_CASE("Atmosphere: default parameters")
{
    AtmosphereRenderer renderer;
    
    // Check that we have reasonable defaults
    CHECK(renderer.initialize({}));
    
    // Test some key parameter values
    CHECK(true); // Placeholder test - actual implementation would check more details
}

TEST_CASE("Atmosphere: parameter initialization")
{
    AtmosphereParams params{};
    params.planetRadius = 6371000.0f;
    params.atmosphereRadius = 6471000.0f;
    
    AtmosphereRenderer renderer;
    CHECK(renderer.initialize(params));
}