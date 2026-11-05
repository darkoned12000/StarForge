#include "planets/cubed_sphere.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::planets
{

CubeFaceBasis getCubeFaceBasis(CubeFace face) noexcept
{
    CubeFaceBasis b;
    switch (face)
    {
    case CubeFace::PosX:
        b.right = math::Vec3d{0.0, 0.0, -1.0};
        b.up = math::Vec3d{0.0, 1.0, 0.0};
        b.normal = math::Vec3d{1.0, 0.0, 0.0};
        break;
    case CubeFace::NegX:
        b.right = math::Vec3d{0.0, 0.0, 1.0};
        b.up = math::Vec3d{0.0, 1.0, 0.0};
        b.normal = math::Vec3d{-1.0, 0.0, 0.0};
        break;
    case CubeFace::PosY:
        b.right = math::Vec3d{1.0, 0.0, 0.0};
        b.up = math::Vec3d{0.0, 0.0, -1.0};
        b.normal = math::Vec3d{0.0, 1.0, 0.0};
        break;
    case CubeFace::NegY:
        b.right = math::Vec3d{1.0, 0.0, 0.0};
        b.up = math::Vec3d{0.0, 0.0, 1.0};
        b.normal = math::Vec3d{0.0, -1.0, 0.0};
        break;
    case CubeFace::PosZ:
        b.right = math::Vec3d{1.0, 0.0, 0.0};
        b.up = math::Vec3d{0.0, 1.0, 0.0};
        b.normal = math::Vec3d{0.0, 0.0, 1.0};
        break;
    case CubeFace::NegZ:
        b.right = math::Vec3d{-1.0, 0.0, 0.0};
        b.up = math::Vec3d{0.0, 1.0, 0.0};
        b.normal = math::Vec3d{0.0, 0.0, -1.0};
        break;
    }
    return b;
}

// Tangent-space cubed-sphere mapping (Arvo's method variant)
// Maps (u,v) in [-1,1] on cube face to unit sphere
math::Vec3d cubeToSphere(CubeFace face, double u, double v) noexcept
{
    auto b = getCubeFaceBasis(face);
    // Point on unit cube face
    math::Vec3d p = b.normal + u * b.right + v * b.up;
    // Normalize to get point on unit sphere
    double len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len > 0.0)
    {
        p.x /= len;
        p.y /= len;
        p.z /= len;
    }
    return p;
}

bool sphereToCube(const math::Vec3d& dir, CubeFace& face, double& u, double& v) noexcept
{
    double x = dir.x;
    double y = dir.y;
    double z = dir.z;
    double ax = std::abs(x);
    double ay = std::abs(y);
    double az = std::abs(z);
    
    if (ax < 1e-12 && ay < 1e-12 && az < 1e-12)
    {
        return false;
    }

    if (ax >= ay && ax >= az)
    {
        if (x > 0.0)
        {
            face = CubeFace::PosX;
            u = -z / x;
            v = y / x;
        }
        else
        {
            face = CubeFace::NegX;
            u = -z / x;
            v = -y / x;
        }
    }
    else if (ay >= ax && ay >= az)
    {
        if (y > 0.0)
        {
            face = CubeFace::PosY;
            u = x / y;
            v = -z / y;
        }
        else
        {
            face = CubeFace::NegY;
            u = -x / y;
            v = -z / y;
        }
    }
    else
    {
        if (z > 0.0)
        {
            face = CubeFace::PosZ;
            u = x / z;
            v = y / z;
        }
        else
        {
            face = CubeFace::NegZ;
            u = x / z;
            v = -y / z;
        }
    }
    return true;
}

} // namespace starforge::planets
