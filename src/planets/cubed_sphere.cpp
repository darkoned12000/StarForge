#include "planets/cubed_sphere.hpp"
#include "math/math.hpp"

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
    float length = math::length(outPosition);
    if (length > 0.0f)
    {
        outPosition[0] /= length;
        outPosition[1] /= length; 
        outPosition[2] /= length;
    }
}

bool CubedSphere::sphereToFace(const float* position, CubeFace* outFace, float* outU, float* outV)
{
    // Find the face with maximum absolute coordinate
    float absX = std::abs(position[0]);
    float absY = std::abs(position[1]); 
    float absZ = std::abs(position[2]);
    
    if (absX >= absY && absX >= absZ)
    {
        // X face
        if (position[0] > 0)
        {
            *outFace = CubeFace::PositiveX;
            *outU = -position[2];
            *outV = -position[1];
        }
        else
        {
            *outFace = CubeFace::NegativeX;
            *outU = position[2];
            *outV = -position[1];
        }
    }
    else if (absY >= absZ)
    {
        // Y face  
        if (position[1] > 0)
        {
            *outFace = CubeFace::PositiveY;
            *outU = position[0];
            *outV = position[2];
        }
        else
        {
            *outFace = CubeFace::NegativeY;
            *outU = position[0];
            *outV = -position[2];
        }
    }
    else
    {
        // Z face
        if (position[2] > 0)
        {
            *outFace = CubeFace::PositiveZ;
            *outU = position[0];
            *outV = -position[1];
        }
        else
        {
            *outFace = CubeFace::NegativeZ;
            *outU = -position[0];
            *outV = -position[1];
        }
    }
    
    return true;
}

} // namespace starforge::planets