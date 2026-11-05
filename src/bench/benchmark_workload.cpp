#include "bench/benchmark_workload.hpp"

#include "bench/benchmark_scene.hpp"
#include "math/math.hpp"
#include "spatial/world_position.hpp"
#include "spatial/camera_relative.hpp"

#include <cmath>
#include <cstdint>

namespace starforge::bench
{

BenchmarkScene generateWorkloadScene(const WorkloadConfig& config)
{
    // Map visible fraction to scene radius. The camera is at (0,0,5) looking
    // at origin with 60° FOV. A sphere of radius R around origin has
    // approximately (R/5)^3 * visible_fraction instances visible.
    // We use a continuous mapping: R = 5.0 / cbrt(visibleFraction).
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
    // Camera at (0,0,5) looking down -Z (towards origin).
    math::Mat4d view = glm::lookAt(
        math::Vec3d(0.0, 0.0, 5.0),
        math::Vec3d(0.0, 0.0, 0.0),
        math::Vec3d(0.0, 1.0, 0.0));
    // Reverse-Z: swap near and far for reverse-Z projection.
    math::Mat4d proj = glm::perspective(1.0, 16.0 / 9.0, 10000.0, 0.1);
    math::Mat4d vp = proj * view;
    return buildFrustum(vp);
}

WorkloadResult recordWorkloadFrame(
    const WorkloadConfig& config,
    const BenchmarkScene& scene,
    const Frustum& frustum,
    VkCommandBuffer cmd,
    VkPipelineLayout pipelineLayout,
    VkPipeline pipeline)
{
    WorkloadResult result;

    // Determine visible instances.
    std::vector<std::uint32_t> visibleIndices;
    if (config.enableCulling)
    {
        visibleIndices = cullInstances(scene, frustum, 0.5);
    }
    else
    {
        visibleIndices.resize(scene.instances.size());
        for (std::size_t i = 0; i < scene.instances.size(); ++i)
        {
            visibleIndices[i] = static_cast<std::uint32_t>(i);
        }
    }

    result.instancesVisible = static_cast<std::uint32_t>(visibleIndices.size());
    result.instancesSubmitted = static_cast<std::uint32_t>(scene.instances.size());

    // Bind pipeline if provided.
    if (pipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // Set viewport and scissor (assuming 1920x1080).
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = static_cast<float>(config.resolutionHeight);
        viewport.width = static_cast<float>(config.resolutionWidth);
        viewport.height = -static_cast<float>(config.resolutionHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {config.resolutionWidth, config.resolutionHeight};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    // Draw each visible instance.
    for (std::uint32_t idx : visibleIndices)
    {
        const auto& inst = scene.instances[idx];

        // Compute camera-relative MVP (identity view for simplicity).
        // In a real benchmark, this would use the full camera transform.
        float mvp[16] = {0};
        mvp[0] = 1.0f; mvp[5] = 1.0f; mvp[10] = 1.0f; mvp[15] = 1.0f;
        mvp[12] = static_cast<float>(inst.posX);
        mvp[13] = static_cast<float>(inst.posY);
        mvp[14] = static_cast<float>(inst.posZ);

        if (pipelineLayout != VK_NULL_HANDLE)
        {
            // Push constants.
            struct PushData { float mvp[16]; uint32_t drawType; } pushData{};
            for (int i = 0; i < 16; ++i) pushData.mvp[i] = mvp[i];
            pushData.drawType = 0; // cube

            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pushData), &pushData);
        }

        if (pipeline != VK_NULL_HANDLE)
        {
            vkCmdDraw(cmd, 36, 1, 0, 0);
        }
        result.drawCalls++;
    }

    return result;
}

} // namespace starforge::bench
