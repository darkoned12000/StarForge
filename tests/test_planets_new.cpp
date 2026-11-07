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
    // Positive X face via the unified free-function API:
    // (1, -0.5, -0.5) / sqrt(1.5), normalized to the unit sphere.
    const Vec3d p = cubeToSphere(CubeFace::PositiveX, 0.5, 0.5);
    CHECK(std::abs(p.x - 0.816496580927726) < 1e-9);
    CHECK(std::abs(p.y - (-0.408248290463863)) < 1e-9);
    CHECK(std::abs(p.z - (-0.408248290463863)) < 1e-9);

    const double length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    CHECK(std::abs(length - 1.0) < 1e-10);
}

TEST_CASE("Atmosphere: default parameters")
{
    AtmosphereRenderer renderer;

    CHECK(renderer.initialize({}, false));

    const auto& p = renderer.params();
    CHECK(p.planetRadius > 0.0f);
    CHECK(p.atmosphereRadius > p.planetRadius);
    for (float c : p.rayleighScatter)
    {
        CHECK(c > 0.0f);
    }
    CHECK(p.mieScatter >= 0.0f);
    CHECK(p.mieG > -1.0f);
    CHECK(p.mieG < 1.0f);
    const double sunLen = std::sqrt(
        p.sunDirection[0] * p.sunDirection[0] +
        p.sunDirection[1] * p.sunDirection[1] +
        p.sunDirection[2] * p.sunDirection[2]);
    CHECK(std::abs(sunLen - 1.0) < 1e-5);
    CHECK(p.sunIrradiance > 0.0f);
}

TEST_CASE("Atmosphere: parameter initialization")
{
    AtmosphereParams params{};
    params.planetRadius = 6371000.0f;
    params.atmosphereRadius = 6471000.0f;

    AtmosphereRenderer renderer;
    CHECK(renderer.initialize(params, false));
    CHECK(renderer.lut().data.size() == renderer.lut().width * renderer.lut().height);
}

TEST_CASE("Atmosphere LUT: deterministic across thread counts (7.4a)")
{
    AtmosphereParams params{};
    params.planetRadius = 6371000.0f;
    params.atmosphereRadius = 6471000.0f;

    auto sequential = generateAtmosphereLut(params, false);
    auto parallel = generateAtmosphereLut(params, true);

    CHECK(sequential.width == parallel.width);
    CHECK(sequential.height == parallel.height);
    REQUIRE(sequential.data.size() == parallel.data.size());
    for (std::size_t i = 0; i < sequential.data.size(); ++i)
    {
        CHECK(parallel.data[i][0] == sequential.data[i][0]);
        CHECK(parallel.data[i][1] == sequential.data[i][1]);
        CHECK(parallel.data[i][2] == sequential.data[i][2]);
    }
}

TEST_CASE("Atmosphere LUT: sane range and sun-facing brightness")
{
    AtmosphereParams params{};
    params.planetRadius = 6371000.0f;
    params.atmosphereRadius = 6471000.0f;

    auto lut = generateAtmosphereLut(params, false);
    REQUIRE(lut.data.size() == lut.width * lut.height);

    float maxChannel = 0.0f;
    for (const auto& texel : lut.data)
    {
        for (float c : texel)
        {
            CHECK(c >= 0.0f);
            maxChannel = std::max(maxChannel, c);
        }
    }
    CHECK(maxChannel == doctest::Approx(1.0f).epsilon(0.01f));

    // Directional dependence at FIXED view zenith (muView = 1, zenith):
    // sun overhead (muSun = 1) must outshine sun below horizon (muSun = -1).
    const auto& towardSun = lut.data[(lut.height - 1) * lut.width + (lut.width - 1)];
    const auto& awaySun = lut.data[0 * lut.width + (lut.width - 1)];
    const float towardLum = towardSun[0] + towardSun[1] + towardSun[2];
    const float awayLum = awaySun[0] + awaySun[1] + awaySun[2];
    CHECK(towardLum > awayLum);
}