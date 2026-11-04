#pragma once

// GLM is an implementation detail. Public STARFORGE headers must not include
// GLM directly; only internal headers and .cpp files may.
// GLM_FORCE_* macros are set via CMake compile definitions on starforge_math.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdint>

namespace starforge::math
{

// STARFORGE-owned type aliases. Consumers use these names; GLM types never
// leak through the public API.
using Vec3f = glm::vec3;
using Vec3d = glm::dvec3;
using Vec4f = glm::vec4;
using Vec4d = glm::dvec4;
using Mat3f = glm::mat3;
using Mat3d = glm::dmat3;
using Mat4f = glm::mat4;
using Mat4d = glm::dmat4;

// World-space basis conventions: right-handed, Y-up, camera looks down -Z.
inline constexpr Vec3f kRightX{1.0f, 0.0f, 0.0f};
inline constexpr Vec3f kUpY{0.0f, 1.0f, 0.0f};
inline constexpr Vec3f kForwardZ{0.0f, 0.0f, 1.0f};

// Vulkan clip-space perspective projection (right-handed view space looking
// down -Z, Y-down in clip space, depth in [0, 1] with standard near -> 0, far -> 1).
[[nodiscard]] inline Mat4f perspectiveVulkan(float fovYRadians, float aspect, float nearPlane, float farPlane)
{
    Mat4f proj = glm::perspectiveRH_ZO(fovYRadians, aspect, nearPlane, farPlane);
    proj[1][1] *= -1.0f; // Vulkan clip space has Y pointing down.
    return proj;
}

// Vulkan reverse-Z perspective projection (near -> 1, far -> 0, Y-down clip space).
[[nodiscard]] inline Mat4f perspectiveVulkanReverseZ(float fovYRadians, float aspect, float nearPlane, float farPlane)
{
    Mat4f proj = glm::perspectiveRH_ZO(fovYRadians, aspect, farPlane, nearPlane);
    proj[1][1] *= -1.0f;
    return proj;
}

// Vulkan reverse-Z perspective projection with an infinite far plane
// (z = -nearPlane maps to depth 1, z -> -infinity maps to depth 0, Y-down clip space).
[[nodiscard]] inline Mat4f perspectiveVulkanReverseZInfinite(float fovYRadians, float aspect, float nearPlane)
{
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    Mat4f proj(0.0f);
    proj[0][0] = f / aspect;
    proj[1][1] = -f;        // Y-down for Vulkan clip space
    proj[2][3] = -1.0f;     // w_clip = -z_view
    proj[3][2] = nearPlane; // z_clip = nearPlane -> z_ndc = nearPlane / -z_view
    return proj;
}

} // namespace starforge::math
