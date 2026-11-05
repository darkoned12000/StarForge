#include <doctest/doctest.h>

#include "math/math.hpp"

#include <cmath>

using namespace starforge::math;

namespace
{

constexpr float kEpsilon = 1e-5f;

bool approxEqual(float a, float b, float eps = kEpsilon)
{
    return std::fabs(a - b) < eps;
}

bool approxEqualVec3(const Vec3f& a, const Vec3f& b, float eps = kEpsilon)
{
    return approxEqual(a.x, b.x, eps) &&
           approxEqual(a.y, b.y, eps) &&
           approxEqual(a.z, b.z, eps);
}

} // namespace

TEST_CASE("Math conventions: right-handed coordinate system")
{
    // Cross product of right (X) and up (Y) gives +Z, which is backward
    // (away from the viewer) in a camera-looking-down- -Z convention.
    const Vec3f back = glm::cross(kRightX, kUpY);
    CHECK(approxEqualVec3(back, -kForwardZ));

    const Vec3f negBack = glm::cross(kUpY, kRightX);
    CHECK(approxEqualVec3(negBack, kForwardZ));
}

TEST_CASE("Math conventions: Y-up axis")
{
    CHECK(kUpY.x == 0.0f);
    CHECK(kUpY.y == 1.0f);
    CHECK(kUpY.z == 0.0f);
}

TEST_CASE("Math conventions: column-major storage")
{
    Mat4f m(1.0f);
    m[3] = Vec4f{10.0f, 20.0f, 30.0f, 1.0f}; // translation lives in column 3
    const float* data = glm::value_ptr(m);

    CHECK(approxEqual(data[12], 10.0f));
    CHECK(approxEqual(data[13], 20.0f));
    CHECK(approxEqual(data[14], 30.0f));
    CHECK(approxEqual(data[15], 1.0f));
}

TEST_CASE("Math conventions: Vulkan clip space Y-up (flipped via viewport)")
{
    const float fov = glm::radians(60.0f);
    const float aspect = 16.0f / 9.0f;
    const Mat4f proj = perspectiveVulkanReverseZ(fov, aspect, 0.1f, 100.0f);

    // Point above the origin (positive Y) in front of the camera (-Z) maps to
    // positive clip Y in Y-up clip space. Y-flip occurs at viewport stage.
    const Vec4f pointAbove{0.0f, 1.0f, -5.0f, 1.0f};
    const Vec4f clip = proj * pointAbove;
    CHECK(clip.w > 0.0f);
    CHECK((clip.y / clip.w) > 0.0f);
}

TEST_CASE("Math conventions: depth range 0..1 (Vulkan)")
{
    const float fov = glm::radians(60.0f);
    const float aspect = 16.0f / 9.0f;
    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;

    const Mat4f proj = perspectiveVulkan(fov, aspect, nearPlane, farPlane);

    const Vec4f nearClip = proj * Vec4f{0.0f, 0.0f, -nearPlane, 1.0f};
    const Vec4f farClip = proj * Vec4f{0.0f, 0.0f, -farPlane, 1.0f};

    const float nearDepth = nearClip.z / nearClip.w;
    const float farDepth = farClip.z / farClip.w;

    CHECK(approxEqual(nearDepth, 0.0f));
    CHECK(approxEqual(farDepth, 1.0f));
}

TEST_CASE("Math conventions: reverse-Z (near -> 1, far -> 0) and infinite far plane")
{
    const float fov = glm::radians(60.0f);
    const float aspect = 16.0f / 9.0f;
    const float nearPlane = 0.1f;
    const float farPlane = 1000.0f;

    const Mat4f proj = perspectiveVulkanReverseZ(fov, aspect, nearPlane, farPlane);

    const Vec4f nearClip = proj * Vec4f{0.0f, 0.0f, -nearPlane, 1.0f};
    const Vec4f farClip = proj * Vec4f{0.0f, 0.0f, -farPlane, 1.0f};

    CHECK(approxEqual(nearClip.z / nearClip.w, 1.0f));
    CHECK(approxEqual(farClip.z / farClip.w, 0.0f));

    // Near depth > far depth (reverse-Z: near maps to 1, far maps to 0).
    CHECK((nearClip.z / nearClip.w) > (farClip.z / farClip.w));

    // Infinite far plane reverse-Z: near -> 1, large distance -> approaches 0+.
    const Mat4f infProj = perspectiveVulkanReverseZInfinite(fov, aspect, nearPlane);
    const Vec4f infNear = infProj * Vec4f{0.0f, 0.0f, -nearPlane, 1.0f};
    const Vec4f infFar = infProj * Vec4f{0.0f, 0.0f, -1e6f, 1.0f};

    CHECK(approxEqual(infNear.z / infNear.w, 1.0f));
    CHECK((infFar.z / infFar.w) > 0.0f);
    CHECK((infFar.z / infFar.w) < 1e-5f);

    // Depth ordering: a point at 10 m should have depth between near (1) and far (0).
    const Vec4f midClip = infProj * Vec4f{0.0f, 0.0f, -10.0f, 1.0f};
    const float midDepth = midClip.z / midClip.w;
    CHECK(midDepth > 0.0f);
    CHECK(midDepth < 1.0f);
    CHECK(midDepth > (infFar.z / infFar.w)); // 10 m is closer than 1e6 m
}

TEST_CASE("Math conventions: infinite far plane depth ordering at multiple distances")
{
    const float fov = glm::radians(60.0f);
    const float aspect = 16.0f / 9.0f;
    const float nearPlane = 0.1f;

    const Mat4f infProj = perspectiveVulkanReverseZInfinite(fov, aspect, nearPlane);

    // Depth should monotonically decrease as distance increases.
    const float distances[] = {0.1f, 1.0f, 10.0f, 100.0f, 1000.0f, 1e6f};
    float prevDepth = 2.0f; // start above 1.0

    for (float d : distances)
    {
        const Vec4f clip = infProj * Vec4f{0.0f, 0.0f, -d, 1.0f};
        const float depth = clip.z / clip.w;
        CHECK(depth >= 0.0f);
        CHECK(depth <= 1.0f);
        CHECK(depth < prevDepth); // monotonically decreasing
        prevDepth = depth;
    }
}
