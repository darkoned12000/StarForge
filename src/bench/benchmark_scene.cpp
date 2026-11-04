#include "bench/benchmark_scene.hpp"

#include <cmath>
#include <random>

namespace starforge::bench
{

BenchmarkScene generateScene(std::uint64_t seed, std::uint32_t instanceCount, double radius)
{
    BenchmarkScene scene;
    scene.seed = seed;
    scene.radius = radius;
    scene.instanceCount = instanceCount;
    scene.instances.reserve(instanceCount);

    std::mt19937_64 rng(seed);

    std::uniform_real_distribution<double> posDist(-radius, radius);
    std::uniform_real_distribution<float> scaleDist(0.5f, 2.0f);
    std::normal_distribution<float> normDist(0.0f, 1.0f);

    for (std::uint32_t i = 0; i < instanceCount; ++i)
    {
        SceneInstance inst;

        // Uniform in sphere: rejection sampling.
        double x, y, z;
        do
        {
            x = posDist(rng);
            y = posDist(rng);
            z = posDist(rng);
        } while (x * x + y * y + z * z > radius * radius);

        inst.posX = x;
        inst.posY = y;
        inst.posZ = z;

        // Random quaternion (uniform on SO(3) via Gaussian).
        float qx = normDist(rng);
        float qy = normDist(rng);
        float qz = normDist(rng);
        float qw = normDist(rng);
        float lenSq = qx * qx + qy * qy + qz * qz + qw * qw;
        if (lenSq > 0.0f)
        {
            float invLen = 1.0f / std::sqrt(lenSq);
            inst.rotX = qx * invLen;
            inst.rotY = qy * invLen;
            inst.rotZ = qz * invLen;
            inst.rotW = qw * invLen;
        }
        else
        {
            inst.rotX = 0.0f;
            inst.rotY = 0.0f;
            inst.rotZ = 0.0f;
            inst.rotW = 1.0f;
        }

        inst.scale = scaleDist(rng);

        scene.instances.push_back(inst);
    }

    return scene;
}

Frustum buildFrustum(const math::Mat4d& viewProj)
{
    Frustum frustum{};

    // Extract planes from the view-projection matrix.
    // For column-major matrix m[col][row], the planes are:
    // Left:   row3 + row0
    // Right:  row3 - row0
    // Bottom: row3 + row1
    // Top:    row3 - row1
    // Near:   row3 + row2 (or row2 for [0,1] depth)
    // Far:    row3 - row2

    // viewProj is column-major: m[col][row]
    // Row i = (m[0][i], m[1][i], m[2][i], m[3][i])

    // Left plane: row3 + row0
    frustum.planes[0][0] = viewProj[0][3] + viewProj[0][0];
    frustum.planes[0][1] = viewProj[1][3] + viewProj[1][0];
    frustum.planes[0][2] = viewProj[2][3] + viewProj[2][0];
    frustum.planes[0][3] = viewProj[3][3] + viewProj[3][0];

    // Right plane: row3 - row0
    frustum.planes[1][0] = viewProj[0][3] - viewProj[0][0];
    frustum.planes[1][1] = viewProj[1][3] - viewProj[1][0];
    frustum.planes[1][2] = viewProj[2][3] - viewProj[2][0];
    frustum.planes[1][3] = viewProj[3][3] - viewProj[3][0];

    // Bottom plane: row3 + row1
    frustum.planes[2][0] = viewProj[0][3] + viewProj[0][1];
    frustum.planes[2][1] = viewProj[1][3] + viewProj[1][1];
    frustum.planes[2][2] = viewProj[2][3] + viewProj[2][1];
    frustum.planes[2][3] = viewProj[3][3] + viewProj[3][1];

    // Top plane: row3 - row1
    frustum.planes[3][0] = viewProj[0][3] - viewProj[0][1];
    frustum.planes[3][1] = viewProj[1][3] - viewProj[1][1];
    frustum.planes[3][2] = viewProj[2][3] - viewProj[2][1];
    frustum.planes[3][3] = viewProj[3][3] - viewProj[3][1];

    // Near plane: row2 (for [0,1] depth, near maps to 1, so near = row3 - row2 for reverse-Z)
    // Actually for standard projection, near = row3 + row2. For reverse-Z, near = row3 - row2.
    // We use reverse-Z (near -> 1, far -> 0), so near plane is row3 - row2.
    frustum.planes[4][0] = viewProj[0][3] - viewProj[0][2];
    frustum.planes[4][1] = viewProj[1][3] - viewProj[1][2];
    frustum.planes[4][2] = viewProj[2][3] - viewProj[2][2];
    frustum.planes[4][3] = viewProj[3][3] - viewProj[3][2];

    // Far plane: row2 (far maps to 0.0)
    frustum.planes[5][0] = viewProj[0][2];
    frustum.planes[5][1] = viewProj[1][2];
    frustum.planes[5][2] = viewProj[2][2];
    frustum.planes[5][3] = viewProj[3][2];

    // Normalize planes.
    for (int i = 0; i < 6; ++i)
    {
        double len = std::sqrt(
            frustum.planes[i][0] * frustum.planes[i][0] +
            frustum.planes[i][1] * frustum.planes[i][1] +
            frustum.planes[i][2] * frustum.planes[i][2]);
        if (len > 0.0)
        {
            for (int j = 0; j < 4; ++j)
            {
                frustum.planes[i][j] /= len;
            }
        }
    }

    return frustum;
}

bool sphereInFrustum(const Frustum& frustum, double cx, double cy, double cz, double radius)
{
    for (int i = 0; i < 6; ++i)
    {
        double dist = frustum.planes[i][0] * cx +
                      frustum.planes[i][1] * cy +
                      frustum.planes[i][2] * cz +
                      frustum.planes[i][3];
        if (dist < -radius)
        {
            return false; // entirely outside
        }
    }
    return true;
}

std::vector<std::uint32_t> cullInstances(const BenchmarkScene& scene, const Frustum& frustum, double instanceRadius)
{
    std::vector<std::uint32_t> visible;
    visible.reserve(scene.instances.size());
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(scene.instances.size()); ++i)
    {
        const auto& inst = scene.instances[i];
        if (sphereInFrustum(frustum, inst.posX, inst.posY, inst.posZ, instanceRadius * inst.scale))
        {
            visible.push_back(i);
        }
    }
    return visible;
}

} // namespace starforge::bench
