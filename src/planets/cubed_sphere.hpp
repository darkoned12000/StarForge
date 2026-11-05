#pragma once

#include "math/math.hpp"

#include <array>
#include <cstdint>

namespace starforge::planets
{

// Cube face indices for a cubed-sphere.
enum class CubeFace : std::uint8_t
{
    PosX = 0,
    NegX = 1,
    PosY = 2,
    NegY = 3,
    PosZ = 4,
    NegZ = 5,
};

// Convert a point on the unit cube face to a point on the unit sphere.
// Uses linear gnomonic projection onto the unit sphere for reduced distortion.
[[nodiscard]] math::Vec3d cubeToSphere(CubeFace face, double u, double v) noexcept;

// Convert a unit sphere direction to (face, u, v) on the cubed-sphere.
// u,v are in [-1, 1].
[[nodiscard]] bool sphereToCube(const math::Vec3d& dir, CubeFace& face, double& u, double& v) noexcept;

// Get the tangent-space basis for a cube face.
struct CubeFaceBasis
{
    math::Vec3d right;
    math::Vec3d up;
    math::Vec3d normal;
};
[[nodiscard]] CubeFaceBasis getCubeFaceBasis(CubeFace face) noexcept;

} // namespace starforge::planets
