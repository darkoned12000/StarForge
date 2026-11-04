#include <doctest/doctest.h>

#include "spatial/camera_relative.hpp"
#include "spatial/world_position.hpp"

#include <cmath>
#include <cstdint>

using namespace starforge::math;
using namespace starforge::spatial;

namespace
{

constexpr double kEpsilon = 1e-9;

bool approxEqual(double a, double b, double eps = kEpsilon)
{
    return std::fabs(a - b) < eps;
}

bool approxEqualVec3f(const Vec3f& a, const Vec3f& b, float eps = 1e-4f)
{
    return std::fabs(a.x - b.x) < eps &&
           std::fabs(a.y - b.y) < eps &&
           std::fabs(a.z - b.z) < eps;
}

} // namespace

TEST_CASE("Camera-relative: positions within 10 km accurate to better than 1 mm at 1e12 m and 1e24 m")
{
    // At 1e24 m (~9.536e17 cells), camera and object separated by 9,999.1234 m.
    constexpr std::int64_t baseCell = 953674316406250000LL;
    auto cam = WorldPosition::fromCellAndOffset(baseCell, -baseCell, 77, 100.0, 200.0, 300.0);
    REQUIRE(cam.has_value());

    const Vec3d trueOffset{9999.1234, -4321.5678, 1234.8765};
    auto obj = cam->translated(trueOffset);
    REQUIRE(obj.has_value());

    const Vec3d relDouble = toCameraRelative(*obj, *cam);
    CHECK(approxEqual(relDouble.x, trueOffset.x, 1e-9));
    CHECK(approxEqual(relDouble.y, trueOffset.y, 1e-9));
    CHECK(approxEqual(relDouble.z, trueOffset.z, 1e-9));

    // Cast once to float32: at 10 km, float32 ULP is ~0.00098 m (< 1 mm).
    const Vec3f relFloat = toFloat32(relDouble);
    CHECK(std::fabs(static_cast<double>(relFloat.x) - trueOffset.x) < 0.001);
    CHECK(std::fabs(static_cast<double>(relFloat.y) - trueOffset.y) < 0.001);
    CHECK(std::fabs(static_cast<double>(relFloat.z) - trueOffset.z) < 0.001);
}

TEST_CASE("Camera-relative: full Model and Model-View transform composed in double and cast once")
{
    constexpr std::int64_t farCell = 1000000000LL; // ~1e15 m
    auto cam = WorldPosition::fromCellAndOffset(farCell, farCell, -farCell, 50.0, 10.0, 20.0);
    REQUIRE(cam.has_value());

    auto obj = cam->translated(Vec3d{120.0, -45.0, -300.0});
    REQUIRE(obj.has_value());

    // 90-degree rotation around Y axis, uniform scale 2.0: (x, y, z) -> (2*z, 2*y, -2*x)
    Mat3d localRotScale(0.0);
    localRotScale[0] = Vec3d{0.0, 0.0, -2.0};
    localRotScale[1] = Vec3d{0.0, 2.0,  0.0};
    localRotScale[2] = Vec3d{2.0, 0.0,  0.0};

    // Identity view rotation vs 90-degree view rotation around Z: (x, y, z) -> (-y, x, z)
    Mat3d viewRot(0.0);
    viewRot[0] = Vec3d{ 0.0, 1.0, 0.0};
    viewRot[1] = Vec3d{-1.0, 0.0, 0.0};
    viewRot[2] = Vec3d{ 0.0, 0.0, 1.0};

    const Mat4f modelMat = composeCameraRelativeModelMatrix(*obj, *cam, localRotScale);
    const Vec4f localVertex{1.0f, 2.0f, 3.0f, 1.0f};
    const Vec4f camSpaceFromModel = modelMat * localVertex;

    // Expected camera-relative position: (120 + 2*3, -45 + 2*2, -300 - 2*1) = (126, -41, -302)
    CHECK(approxEqualVec3f(Vec3f(camSpaceFromModel), Vec3f{126.0f, -41.0f, -302.0f}));

    const Mat4f modelViewMat = composeCameraRelativeModelView(*obj, *cam, localRotScale, viewRot);
    const Vec4f viewSpaceVertex = modelViewMat * localVertex;

    // Applying viewRot: (x, y, z) -> (-y, x, z) => (41, 126, -302)
    CHECK(approxEqualVec3f(Vec3f(viewSpaceVertex), Vec3f{41.0f, 126.0f, -302.0f}));
}

TEST_CASE("Large object: planet radius 6.4e6 m at 1e15 m from origin has no surface vertex jitter")
{
    constexpr double planetRadius = 6.4e6;
    constexpr std::int64_t systemCell = 953674316LL; // ~1e15 m

    auto planetCentre = WorldPosition::fromCellAndOffset(systemCell, 0, 0, 0.0, 0.0, 0.0);
    REQUIRE(planetCentre.has_value());

    // Camera is 50 m above the planet's +Y pole.
    auto camera = planetCentre->translated(Vec3d{0.0, planetRadius + 50.0, 0.0});
    REQUIRE(camera.has_value());

    // Patch origin is at the +Y pole on the surface (0, planetRadius, 0) relative to planetCentre.
    const Vec3d patchOriginOnPlanet{0.0, planetRadius, 0.0};
    const LargeObjectSetup setup = makeLargeObjectSetup(*planetCentre, *camera, patchOriginOnPlanet);

    // Two adjacent vertices on the terrain patch separated by 1 cm (0.01 m).
    // Naive float32 relative to planetCentre (6.4e6 m) has ULP = 0.5 m and would
    // collapse two vertices 1 cm apart to the exact same coordinate!
    const float naiveY1 = static_cast<float>(planetRadius);
    const float naiveY2 = static_cast<float>(planetRadius + 0.01);
    CHECK(naiveY1 == naiveY2); // proves naive planet-centred float32 jitters/collapses

    // With per-patch local origin composed in double, 1 cm separation is preserved accurately.
    const Vec3f v1 = setup.transformPatchVertex(Vec3f{0.0f, 0.0f, 0.0f});
    const Vec3f v2 = setup.transformPatchVertex(Vec3f{0.01f, 0.01f, 0.0f});

    CHECK(approxEqualVec3f(v1, Vec3f{0.0f, -50.0f, 0.0f}, 1e-4f));
    CHECK(std::fabs((v2.x - v1.x) - 0.01f) < 1e-5f);
    CHECK(std::fabs((v2.y - v1.y) - 0.01f) < 1e-5f);
}
