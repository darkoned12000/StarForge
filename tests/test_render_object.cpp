#include <doctest/doctest.h>

#include "spatial/render_object.hpp"
#include "spatial/world_position.hpp"

#include <cmath>
#include <cstdint>

using namespace starforge::math;
using namespace starforge::spatial;

namespace
{

bool approxEqual(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) < eps;
}

} // namespace

TEST_CASE("RenderObject: camera-relative model matrix at origin")
{
    RenderObject obj;
    obj.position = *WorldPosition::fromMeters(100.0, 200.0, 300.0);
    obj.localRotationScale = Mat3d(1.0); // identity rotation, unit scale
    obj.vertexBufferAddress = 0x12345678;
    obj.indexBufferAddress = 0x87654321;
    obj.indexCount = 36;
    obj.materialIndex = 0;

    auto camera = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camera.has_value());

    Mat4f model = obj.cameraRelativeModelMatrix(*camera);

    // The translation should be (100, 200, 300) in the model matrix.
    CHECK(approxEqual(model[3][0], 100.0f));
    CHECK(approxEqual(model[3][1], 200.0f));
    CHECK(approxEqual(model[3][2], 300.0f));
    CHECK(approxEqual(model[3][3], 1.0f));
}

TEST_CASE("RenderObject: camera-relative model matrix at 1e12 m")
{
    double offset = 1e12;
    auto objPos = WorldPosition::fromMeters(offset + 100.0, offset + 200.0, offset + 300.0);
    auto camPos = WorldPosition::fromMeters(offset, offset, offset);
    REQUIRE(objPos.has_value());
    REQUIRE(camPos.has_value());

    RenderObject obj;
    obj.position = *objPos;
    obj.localRotationScale = Mat3d(1.0);

    Mat4f model = obj.cameraRelativeModelMatrix(*camPos);

    // Should be identical to the origin case.
    CHECK(approxEqual(model[3][0], 100.0f));
    CHECK(approxEqual(model[3][1], 200.0f));
    CHECK(approxEqual(model[3][2], 300.0f));
}

TEST_CASE("RenderObject: camera-relative model matrix with rotation")
{
    RenderObject obj;
    obj.position = *WorldPosition::fromMeters(0.0, 0.0, 0.0);

    // 90-degree rotation around Y: (x, y, z) -> (z, y, -x)
    Mat3d rot(0.0);
    rot[0] = Vec3d{0.0, 0.0, -1.0};
    rot[1] = Vec3d{0.0, 1.0, 0.0};
    rot[2] = Vec3d{1.0, 0.0, 0.0};
    obj.localRotationScale = rot;

    auto camera = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camera.has_value());

    Mat4f model = obj.cameraRelativeModelMatrix(*camera);

    // Apply rotation to a vertex at (1, 0, 0) -> should be (0, 0, -1).
    Vec4f v{1.0f, 0.0f, 0.0f, 1.0f};
    Vec4f transformed = model * v;
    CHECK(approxEqual(transformed.x, 0.0f));
    CHECK(approxEqual(transformed.y, 0.0f));
    CHECK(approxEqual(transformed.z, -1.0f));
}

TEST_CASE("RenderObject: composeRenderObjectModelMatrix helper")
{
    RenderObject obj;
    obj.position = *WorldPosition::fromMeters(50.0, 75.0, -25.0);
    obj.localRotationScale = Mat3d(2.0); // uniform scale 2

    auto camera = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camera.has_value());

    Mat4f model = composeRenderObjectModelMatrix(obj, *camera);

    // Translation should be (50, 75, -25).
    CHECK(approxEqual(model[3][0], 50.0f));
    CHECK(approxEqual(model[3][1], 75.0f));
    CHECK(approxEqual(model[3][2], -25.0f));

    // Scale 2: vertex (1,0,0) -> (2, 0, 0) + translation.
    Vec4f v{1.0f, 0.0f, 0.0f, 1.0f};
    Vec4f transformed = model * v;
    CHECK(approxEqual(transformed.x, 52.0f));
    CHECK(approxEqual(transformed.y, 75.0f));
    CHECK(approxEqual(transformed.z, -25.0f));
}

TEST_CASE("RenderObject: large-object setup with per-object local origin")
{
    // Planet at 1e12 m from origin, camera near surface.
    double planetX = 1e12;
    double planetRadius = 6.4e6;

    auto planetCentre = WorldPosition::fromMeters(planetX, 0.0, 0.0);
    auto camera = WorldPosition::fromMeters(planetX, planetRadius + 50.0, 0.0);
    REQUIRE(planetCentre.has_value());
    REQUIRE(camera.has_value());

    RenderObject planet;
    planet.position = *planetCentre;
    planet.localRotationScale = Mat3d(1.0);

    // Camera-relative model matrix should place planet centre at (0, -(radius+50), 0).
    Mat4f model = planet.cameraRelativeModelMatrix(*camera);
    CHECK(approxEqual(model[3][0], 0.0f));
    CHECK(approxEqual(model[3][1], -(float)(planetRadius + 50.0), 1.0f));
    CHECK(approxEqual(model[3][2], 0.0f));
}
