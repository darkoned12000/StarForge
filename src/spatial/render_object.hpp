#pragma once

#include "spatial/world_position.hpp"
#include "spatial/camera_relative.hpp"
#include "math/math.hpp"

#include <cstdint>
#include <vector>

namespace starforge::spatial
{

// A minimal render object for M3: a mesh (identified by vertex/index buffer
// addresses) placed at a WorldPosition with a local rotation and scale.
// The CPU composes a camera-relative Model matrix in double precision and
// casts to float32 once before uploading via push constants.
struct RenderObject
{
    WorldPosition position;
    math::Mat3d localRotationScale = math::Mat3d(1.0); // rotation + scale
    std::uint64_t vertexBufferAddress = 0;             // BDA for vertex data
    std::uint64_t indexBufferAddress = 0;              // BDA for index data
    std::uint32_t indexCount = 0;
    std::uint32_t materialIndex = 0;

    // Compose the camera-relative Model matrix in double, then cast to
    // float32 once. This is the single point where astronomical coordinates
    // are reduced to GPU-compatible precision.
    [[nodiscard]] math::Mat4f cameraRelativeModelMatrix(const WorldPosition& camera) const noexcept;
};

// Compute the camera-relative Model matrix for a render object.
[[nodiscard]] math::Mat4f composeRenderObjectModelMatrix(
    const RenderObject& obj,
    const WorldPosition& camera) noexcept;

} // namespace starforge::spatial
