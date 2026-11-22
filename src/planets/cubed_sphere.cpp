#include "planets/cubed_sphere.hpp"
#include "math/math.hpp"

#include <cmath>

namespace starforge::planets
{

void CubedSphere::faceToSphere(int32_t face, float u, float v, float* outPosition)
{
    if (outPosition == nullptr || face < 0 || face >= getFaceCount())
    {
        if (outPosition != nullptr)
        {
            outPosition[0] = 0.0f;
            outPosition[1] = 0.0f;
            outPosition[2] = 0.0f;
        }
        return;
    }

    const math::Vec3d p = cubeToSphere(static_cast<CubeFace>(face), static_cast<double>(u), static_cast<double>(v));
    outPosition[0] = static_cast<float>(p.x);
    outPosition[1] = static_cast<float>(p.y);
    outPosition[2] = static_cast<float>(p.z);
}

bool CubedSphere::sphereToFace(const float* position, CubeFace* outFace, float* outU, float* outV)
{
    if (position == nullptr || outFace == nullptr || outU == nullptr || outV == nullptr)
    {
        return false;
    }

    double u = 0.0;
    double v = 0.0;
    CubeFace face = CubeFace::PositiveX;
    const bool ok = sphereToCube(
        math::Vec3d{static_cast<double>(position[0]), static_cast<double>(position[1]), static_cast<double>(position[2])},
        face, u, v);
    if (!ok)
    {
        return false;
    }

    *outFace = face;
    *outU = static_cast<float>(u);
    *outV = static_cast<float>(v);
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

CubePatchUvBounds cubePatchUvBounds(std::int32_t x, std::int32_t y, std::uint32_t level)
{
    const std::uint32_t safeLevel = level > 20u ? 20u : level;
    const double span = 2.0 / static_cast<double>(1u << safeLevel);
    CubePatchUvBounds bounds;
    bounds.uMin = -1.0 + static_cast<double>(x) * span;
    bounds.uMax = bounds.uMin + span;
    bounds.vMin = -1.0 + static_cast<double>(y) * span;
    bounds.vMax = bounds.vMin + span;
    return bounds;
}

bool sphereToCube(const math::Vec3d& position, CubeFace& outFace, double& outU, double& outV)
{
    // Double-precision inverse of cubeToSphere (do not route through the
    // float API: that would lose precision below the 1e-10 round-trip
    // tolerance the tests require).
    const double absX = std::abs(position.x);
    const double absY = std::abs(position.y);
    const double absZ = std::abs(position.z);

    if (absX == 0.0 && absY == 0.0 && absZ == 0.0)
    {
        return false;
    }

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