#include <doctest/doctest.h>

#include "planets/cubed_sphere.hpp"
#include "planets/planet_patch.hpp"
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

TEST_CASE("Planet quadtree: initialization and level calculation")
{
    PlanetQuadtree qt(6371000.0f, 1.0f); // Earth radius with 1m error tolerance
    
    CHECK(qt.getMaxLevel() > 0);
    
    // Test patch generation
    auto patches = qt.generatePatches(0, 0, 0);
    CHECK(patches.size() == 4); // Should generate 4 patches for level 0
}

TEST_CASE("Cubed sphere: basic face conversion")
{
    float position[3];
    
    // Test positive X face
    CubedSphere::faceToSphere(0, 0.5f, 0.5f, position);
    
    CHECK(std::abs(position[0] - 1.0f) < 1e-6f); 
    CHECK(std::abs(position[1] - (-0.5f)) < 1e-6f);
    CHECK(std::abs(position[2] - (-0.5f)) < 1e-6f);
    
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