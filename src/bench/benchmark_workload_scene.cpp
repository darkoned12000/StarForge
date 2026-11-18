#include "bench/benchmark_workload.hpp"

#include "bench/benchmark_scene.hpp"
#include "math/math.hpp"

#include <cmath>

// Loader-free scene/frustum construction (M5 Phase 1). Recording workload
// frames (which calls vk* entry points) stays in benchmark_workload.cpp.

namespace starforge::bench
{

BenchmarkScene generateWorkloadScene(const WorkloadConfig& config)
{
    // Map visible fraction to scene radius. The camera is at (0,0,5) looking
    // at origin with 60° FOV. A sphere of radius R around origin has
    // approximately (R/5)^3 * visible_fraction instances visible.
    double radius = 5.0;
    if (config.visibleFraction > 0.0 && config.visibleFraction <= 1.0)
    {
        radius = 5.0 / std::cbrt(config.visibleFraction);
    }
    if (radius < 1.0) radius = 1.0;

    return generateScene(config.seed, config.instanceCount, radius);
}

Frustum buildWorkloadFrustum(const WorkloadConfig& /*config*/)
{
    // Camera at (0,0,5) looking down -Z (towards origin). Use the engine's
    // reverse-Z projection rather than a raw glm::perspective with swapped
    // near/far — buildFrustum's near-plane extraction assumes reverse-Z.
    const math::Mat4d view = glm::lookAt(
        math::Vec3d(0.0, 0.0, 5.0),
        math::Vec3d(0.0, 0.0, 0.0),
        math::Vec3d(0.0, 1.0, 0.0));
    const math::Mat4d proj = math::perspectiveVulkanReverseZ(1.0f, 16.0f / 9.0f, 0.1f, 10000.0f);
    return buildFrustum(proj * view);
}

} // namespace starforge::bench
