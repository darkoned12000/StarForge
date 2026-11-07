#include "planets/cubed_sphere.hpp"
#include "math/math.hpp"

#include <cmath>

namespace starforge::planets
{

void CubedSphere::faceToSphere(int32_t face, float u, float v, float* outPosition)
{
    switch (static_cast<CubeFace>(face))
    {
        case CubeFace::PositiveX:
            outPosition[0] = 1.0f;
            outPosition[1] = -v;
            outPosition[2] = -u;
            break;
        case CubeFace::NegativeX:
            outPosition[0] = -1.0f;
            outPosition[1] = -v;
            outPosition[2] = u;
            break;
        case CubeFace::PositiveY:
            outPosition[0] = u;
            outPosition[1] = 1.0f;
            outPosition[2] = v;
            break;
        case CubeFace::NegativeY:
            outPosition[0] = u;
            outPosition[1] = -1.0f;
            outPosition[2] = -v;
            break;
        case CubeFace::PositiveZ:
            outPosition[0] = u;
            outPosition[1] = -v;
            outPosition[2] = 1.0f;
            break;
        case CubeFace::NegativeZ:
            outPosition[0] = -u;
            outPosition[1] = -v;
            outPosition[2] = -1.0f;
            break;
    }
    
    // Normalize to unit sphere
    const float len = std::sqrt(outPosition[0] * outPosition[0] +
                                outPosition[1] * outPosition[1] +
                                outPosition[2] * outPosition[2]);
    if (len > 0.0f)
    {
        outPosition[0] /= len;
        outPosition[1] /= len;
        outPosition[2] /= len;
    }
}

bool CubedSphere::sphereToFace(const float* position, CubeFace* outFace, float* outU, float* outV)
{
    // Find the face with maximum absolute coordinate
    float absX = std::abs(position[0]);
    float absY = std::abs(position[1]); 
    float absZ = std::abs(position[2]);
    
    // Divide by the absolute dominant component to invert the normalization
    // performed by faceToSphere (position is a unit-sphere direction).
    if (absX >= absY && absX >= absZ)
    {
        // X face
        if (position[0] > 0)
        {
            *outFace = CubeFace::PositiveX;
            *outU = -position[2] / absX;
            *outV = -position[1] / absX;
        }
        else
        {
            *outFace = CubeFace::NegativeX;
            *outU = position[2] / absX;
            *outV = -position[1] / absX;
        }
    }
    else if (absY >= absZ)
    {
        // Y face
        if (position[1] > 0)
        {
            *outFace = CubeFace::PositiveY;
            *outU = position[0] / absY;
            *outV = position[2] / absY;
        }
        else
        {
            *outFace = CubeFace::NegativeY;
            *outU = position[0] / absY;
            *outV = -position[2] / absY;
        }
    }
    else
    {
        // Z face
        if (position[2] > 0)
        {
            *outFace = CubeFace::PositiveZ;
            *outU = position[0] / absZ;
            *outV = -position[1] / absZ;
        }
        else
        {
            *outFace = CubeFace::NegativeZ;
            *outU = -position[0] / absZ;
            *outV = -position[1] / absZ;
        }
    }
    
    return true;
}

math::Vec3d cubeToSphere(CubeFace face, double u, double v)
{
    math::Vec3d p{};
    switch (face)
    {
        case CubeFace::PositiveX: p = {1.0, -v, -u}; break;
        case CubeFace::NegativeX: p = {-1.0, -v, u}; break;
        case CubeFace::PositiveY: p = {u, 1.0, v}; break;
        case CubeFace::NegativeY: p = {u, -1.0, -v}; break;
        case CubeFace::PositiveZ: p = {u, -v, 1.0}; break;
        case CubeFace::NegativeZ: p = {-u, -v, -1.0}; break;
    }
    const double len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len > 0.0)
    {
        p /= len;
    }
    return p;
}

bool sphereToCube(const math::Vec3d& position, CubeFace& outFace, double& outU, double& outV)
{
    // Double-precision inverse of cubeToSphere (do not route through the
    // float API: that would lose precision below the 1e-10 round-trip
    // tolerance the tests require).
    const double absX = std::abs(position.x);
    const double absY = std::abs(position.y);
    const double absZ = std::abs(position.z);

    if (absX >= absY && absX >= absZ)
    {
        if (position.x > 0.0)
        {
            outFace = CubeFace::PositiveX;
            outU = -position.z / absX;
            outV = -position.y / absX;
        }
        else
        {
            outFace = CubeFace::NegativeX;
            outU = position.z / absX;
            outV = -position.y / absX;
        }
    }
    else if (absY >= absZ)
    {
        if (position.y > 0.0)
        {
            outFace = CubeFace::PositiveY;
            outU = position.x / absY;
            outV = position.z / absY;
        }
        else
        {
            outFace = CubeFace::NegativeY;
            outU = position.x / absY;
            outV = -position.z / absY;
        }
    }
    else
    {
        if (position.z > 0.0)
        {
            outFace = CubeFace::PositiveZ;
            outU = position.x / absZ;
            outV = -position.y / absZ;
        }
        else
        {
            outFace = CubeFace::NegativeZ;
            outU = -position.x / absZ;
            outV = -position.y / absZ;
        }
    }
    return true;
}

} // namespace starforge::planets