#pragma once

#include "math/math.hpp"

#include <cstdint>
#include <vector>

namespace starforge::planets
{

// Cube face identifiers for cubed sphere projection
enum class CubeFace : std::uint8_t
{
    PositiveX = 0,
    NegativeX = 1, 
    PositiveY = 2,
    NegativeY = 3,
    PositiveZ = 4,
    NegativeZ = 5
};

// Cubed sphere projection utilities for planetary rendering
class CubedSphere
{
public:
    // Legacy float wrapper. Prefer the double-precision free functions
    // cubeToSphere/sphereToCube below (unified API used by patches/tests).
    [[deprecated("Use cubeToSphere/sphereToCube free functions")]]
    static void faceToSphere(int32_t face, float u, float v, float* outPosition);

    // Convert world position back to cube face coordinates and UV
    [[deprecated("Use cubeToSphere/sphereToCube free functions (sphereToCube)")]]
    static bool sphereToFace(const float* position, CubeFace* outFace, float* outU, float* outV);
    
    // Get the 6 faces of the cubed sphere
    static constexpr int32_t getFaceCount() { return 6; }
};

// Double-precision free-function forms (used by PlanetQuadtree and tests).
// cubeToSphere maps a face-local (u, v) in [-1, 1]^2 to a unit-sphere direction.
// sphereToCube is its inverse: given a unit-sphere direction it recovers the
// dominant face and the face-local (u, v).
[[nodiscard]] math::Vec3d cubeToSphere(CubeFace face, double u, double v);
[[nodiscard]] bool sphereToCube(const math::Vec3d& position, CubeFace& outFace, double& outU, double& outV);

} // namespace starforge::planets