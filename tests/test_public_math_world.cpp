// Public math/world API tests. These include only public headers, proving
// consumers do not need GLM, Vulkan, SDL, or internal src/ headers.
#include <doctest/doctest.h>

#include "starforge/math.hpp"
#include "starforge/world.hpp"

#include <cmath>

namespace
{

[[nodiscard]] bool near(float lhs, float rhs, float epsilon = 1.0e-5f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

} // namespace

TEST_CASE("Public math: vector arithmetic and length")
{
    using namespace starforge::math;
    const Vec3f a{1.0f, 2.0f, 3.0f};
    const Vec3f b{4.0f, 5.0f, 6.0f};
    CHECK(a + b == Vec3f{5.0f, 7.0f, 9.0f});
    CHECK(b - a == Vec3f{3.0f, 3.0f, 3.0f});
    CHECK(a * 2.0f == Vec3f{2.0f, 4.0f, 6.0f});
    CHECK(2.0f * a == Vec3f{2.0f, 4.0f, 6.0f});
    CHECK(near(length(Vec3f{3.0f, 4.0f, 0.0f}), 5.0f));
    CHECK(near(normalize(Vec3f{0.0f, 3.0f, 0.0f}).y(), 1.0f));
    CHECK(dot(Vec3f{1.0f, 0.0f, 0.0f}, Vec3f{0.0f, 1.0f, 0.0f}) == 0.0f);
    CHECK(cross(kRightX, kUpY) == Vec3f{0.0f, 0.0f, 1.0f});

    // Mutable named components and const element references.
    Vec3f mutable_ = a;
    mutable_.x() = 9.0f;
    CHECK(mutable_ == Vec3f{9.0f, 2.0f, 3.0f});
    const Vec3f& constRef = mutable_;
    const float* element = &constRef[1];
    CHECK(element != nullptr);
    CHECK(*element == 2.0f);

    // Unary negation, scalar compound assignment, component-wise product.
    CHECK(-a == Vec3f{-1.0f, -2.0f, -3.0f});
    Vec3f scaled = a;
    scaled *= 3.0f;
    CHECK(scaled == Vec3f{3.0f, 6.0f, 9.0f});
    scaled /= 3.0f;
    CHECK(scaled == a);
    CHECK(a * b == Vec3f{4.0f, 10.0f, 18.0f});

    // Generalized functions across dimensions and scalar types.
    CHECK(near(length(Vec2f{3.0f, 4.0f}), 5.0f));
    CHECK(near(normalize(Vec2f{0.0f, 3.0f}).y(), 1.0f));
    CHECK(dot(Vec3d{1.0, 2.0, 3.0}, Vec3d{4.0, 5.0, 6.0}) == 32.0);
    CHECK(cross(Vec3d{1.0, 0.0, 0.0}, Vec3d{0.0, 1.0, 0.0}) == Vec3d{0.0, 0.0, 1.0});

    // Matrix3 composition: identity is neutral, and A * B applies B first.
    const Mat3d identity(1.0);
    const Mat3d rotation = Mat3d(2.0);
    CHECK(rotation * identity == rotation);
    CHECK(identity * rotation == rotation);
    const auto doubled = (rotation * rotation) * Vec3d{1.0, 0.0, 0.0};
    CHECK(doubled == Vec3d{4.0, 0.0, 0.0});
}

TEST_CASE("Public math: perspective and lookAt conventions")
{
    using namespace starforge::math;
    constexpr float kPi = 3.141592653589793f;
    const auto projection = perspectiveVulkan(kPi / 3.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    // Depth 0..1 (Vulkan clip space), not -1..1.
    CHECK(projection.columns[2][2] < 0.0f);
    CHECK(projection.columns[2][2] > -1.01f);
    CHECK(projection.columns[3][2] < 0.0f);
    CHECK(projection.columns[2][3] == -1.0f);

    const auto view = lookAt(Vec3f{0.0f, 0.0f, 5.0f}, Vec3f{0.0f, 0.0f, 0.0f}, kUpY);
    const auto origin = view * Vec4f{0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(near(origin.x(), 0.0f));
    CHECK(near(origin.y(), 0.0f));
    CHECK(near(origin.z(), -5.0f));
    CHECK(near(origin.w(), 1.0f));
}

TEST_CASE("Public world: fromMeters round-trips and rejects invalid input")
{
    using namespace starforge;
    auto position = WorldPosition::fromMeters(1.0, 2.0, 3.0);
    REQUIRE(position.has_value());
    CHECK(position->toMeters() == math::Vec3d{1.0, 2.0, 3.0});
    CHECK(position->isNormalized());

    // Values beyond one cell normalize into adjacent cells.
    auto far = WorldPosition::fromMeters(WorldPosition::kCellSize + 5.0, 0.0, 0.0);
    REQUIRE(far.has_value());
    CHECK(far->cellX == 1);
    CHECK(far->offsetX == 5.0);

    const auto nan = WorldPosition::fromMeters(std::nan(""), 0.0, 0.0);
    CHECK(!nan.has_value());
}

TEST_CASE("Public world: camera-relative conversion preserves precision at scale")
{
    using namespace starforge;
    const auto origin = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto distant = WorldPosition::fromMeters(1.0e12, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(distant.has_value());

    const auto relative = toCameraRelative(*distant, *origin);
    CHECK(relative.x() == 1.0e12);

    const auto model = composeCameraRelativeModelMatrix(*distant, *origin);
    CHECK(near(model.columns[3][0], 1.0e12f, 1.0e5f));
    CHECK(model.columns[0][0] == 1.0f);
}

TEST_CASE("Public world: large-object setup transforms a patch vertex")
{
    using namespace starforge;
    const auto centre = WorldPosition::fromMeters(1.0e9, 0.0, 0.0);
    const auto camera = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(centre.has_value());
    REQUIRE(camera.has_value());

    const auto setup = makeLargeObjectSetup(*centre, *camera, math::Vec3d{0.0, 0.0, 0.0});
    const auto vertex = setup.transformPatchVertex(math::Vec3f{10.0f, 0.0f, 0.0f});
    CHECK(near(vertex.x(), 1.0e9f + 10.0f, 128.0f));
}
