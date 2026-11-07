#pragma once

// GLM is an implementation detail. Public STARFORGE headers must not include
// GLM directly; only internal headers and .cpp files may.
// Ensure GLM invariants regardless of how this header is included.
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdint>

namespace starforge::math
{

// STARFORGE-owned type aliases. Consumers use these names; GLM types never
// leak through the public API.
using Vec2f = glm::vec2;
using Vec2d = glm::dvec2;
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
inline constexpr Vec3f kForwardZ{0.0f, 0.0f, -1.0f};

// Vulkan clip-space perspective projection (right-handed view space looking
// down -Z, Y-up in clip space, depth in [0, 1] with standard near -> 0, far -> 1).
// Y-flip is handled via negative viewport height, not projection matrix.
[[nodiscard]] inline Mat4f perspectiveVulkan(float fovYRadians, float aspect, float nearPlane, float farPlane)
{
    return glm::perspectiveRH_ZO(fovYRadians, aspect, nearPlane, farPlane);
}

// Vulkan reverse-Z perspective projection (near -> 1, far -> 0, Y-up clip space).
// Y-flip is handled via negative viewport height, not projection matrix.
[[nodiscard]] inline Mat4f perspectiveVulkanReverseZ(float fovYRadians, float aspect, float nearPlane, float farPlane)
{
    return glm::perspectiveRH_ZO(fovYRadians, aspect, farPlane, nearPlane);
}

// Vulkan reverse-Z perspective projection with an infinite far plane
// (z = -nearPlane maps to depth 1, z -> -infinity maps to depth 0, Y-up clip space).
// Y-flip is handled via negative viewport height, not projection matrix.
[[nodiscard]] inline Mat4f perspectiveVulkanReverseZInfinite(float fovYRadians, float aspect, float nearPlane)
{
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    Mat4f proj(0.0f);
    proj[0][0] = f / aspect;
    proj[1][1] = f;         // Y-up for Vulkan clip space (flipped via viewport)
    proj[2][3] = -1.0f;     // w_clip = -z_view
    proj[3][2] = nearPlane; // z_clip = nearPlane -> z_ndc = nearPlane / -z_view
    return proj;
}

} // namespace starforge::math
