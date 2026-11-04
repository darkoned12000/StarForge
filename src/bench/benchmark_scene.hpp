#pragma once

#include "math/math.hpp"
#include "spatial/world_position.hpp"

#include <cstdint>
#include <random>
#include <vector>

namespace starforge::bench
{

// A single instance in a benchmark scene.
struct SceneInstance
{
    double posX, posY, posZ;  // world position (metres)
    float  rotX, rotY, rotZ, rotW; // rotation quaternion
    float  scale = 1.0f;
};

// A deterministic benchmark scene.
struct BenchmarkScene
{
    std::uint64_t seed = 42;
    double radius = 10.0; // sphere radius in metres
    std::vector<SceneInstance> instances;
    std::uint32_t instanceCount = 0;
};

// Generate a deterministic scene from a seed.
[[nodiscard]] BenchmarkScene generateScene(std::uint64_t seed, std::uint32_t instanceCount, double radius);

// Camera frustum for culling.
struct Frustum
{
    // Six planes: (nx, ny, nz, d) where n·p + d >= 0 means inside.
    // Stored as 4 doubles per plane.
    double planes[6][4];
};

// Build a frustum from a view-projection matrix (column-major, double).
[[nodiscard]] Frustum buildFrustum(const math::Mat4d& viewProj);

// Test if a sphere is inside or intersecting the frustum.
[[nodiscard]] bool sphereInFrustum(const Frustum& frustum, double cx, double cy, double cz, double radius);

// CPU frustum culling: returns indices of visible instances.
[[nodiscard]] std::vector<std::uint32_t> cullInstances(const BenchmarkScene& scene, const Frustum& frustum, double instanceRadius = 1.0);

} // namespace starforge::bench
