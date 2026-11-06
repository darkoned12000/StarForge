#pragma once

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
    // Convert cube face coordinates to world position on a unit sphere
    static void faceToSphere(int32_t face, float u, float v, float* outPosition);
    
    // Convert world position back to cube face coordinates and UV
    static bool sphereToFace(const float* position, CubeFace* outFace, float* outU, float* outV);
    
    // Get the 6 faces of the cubed sphere  
    static constexpr int32_t getFaceCount() { return 6; }
};

} // namespace starforge::planets