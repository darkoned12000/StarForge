#include <doctest/doctest.h>

#include "bench/benchmark_scene.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "gpu_resources/gpu_culling.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::bench;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;
using namespace starforge::math;

namespace
{

constexpr float kInstanceRadius = 0.1f;
constexpr std::uint32_t kPushSize = 104; // 24 floats + uint + uint

starforge::bench::BenchmarkScene makeParityScene(std::uint32_t count, double visibleFraction,
                                                  std::uint64_t seed)
{
    starforge::bench::BenchmarkScene scene;
    scene.seed = seed;
    scene.radius = 10.0;
    scene.instanceCount = count;
    scene.instances.reserve(count);

    std::mt19937 rng(static_cast<std::uint32_t>(seed));
    std::uniform_real_distribution<float> jitter(-0.4f, 0.4f);
    const std::uint32_t visibleCount = static_cast<std::uint32_t>(
        std::llround(static_cast<double>(count) * visibleFraction));

    for (std::uint32_t i = 0; i < count; ++i)
    {
        SceneInstance instance{};
        if (i < visibleCount)
        {
            // Well inside the camera frustum, far from every plane boundary.
            instance.posX = jitter(rng);
            instance.posY = jitter(rng);
            instance.posZ = 0.0;
        }
        else
        {
            // Well outside: +X beyond the camera's horizontal field of view.
            instance.posX = 50.0 + jitter(rng);
            instance.posY = 0.0;
            instance.posZ = 0.0;
        }
        instance.scale = kInstanceRadius / 0.5f; // cullInstances uses 0.5 * scale
        scene.instances.push_back(instance);
    }
    return scene;
}

} // namespace

TEST_CASE("GPU cull parity: bit-exact visible sets (100 .. 50k instances, T2)")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();
    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());
        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());

        std::ifstream file(std::string(STARFORGE_SHADER_DIR) + "/cull.spv",
                           std::ios::ate | std::ios::binary);
        REQUIRE(file.good());
        const auto size = static_cast<std::size_t>(file.tellg());
        std::vector<std::uint8_t> bytes(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        auto pipeline = ComputePipeline::create(*device, {
            .spirv = bytes, .entryPoint = "main", .storageBufferCount = 3,
            .pushConstantSize = kPushSize,
        });
        REQUIRE(pipeline.has_value());

        const std::uint32_t instanceCounts[] = {100, 1000, 5000, 50000};
        const double visibleFractions[] = {0.01, 0.1, 1.0};

        for (std::uint32_t count : instanceCounts)
        {
            for (double fraction : visibleFractions)
            {
                const std::uint64_t seed = 1337ULL + count + static_cast<std::uint64_t>(fraction * 100.0);
                const auto scene = makeParityScene(count, fraction, seed);

                const Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
                const Mat4d proj = Mat4d(perspectiveVulkanReverseZ(
                    glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f));
                const Frustum frustum = buildFrustum(proj * view);

                // CPU oracle uses the same float-rounded planes the shader sees.
                float planes[24] = {};
                for (int p = 0; p < 6; ++p)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        planes[p * 4 + c] = static_cast<float>(frustum.planes[p][c]);
                    }
                }
                std::vector<std::uint32_t> cpuVisible;
                std::vector<float> bounds(count * 4);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const auto& instance = scene.instances[i];
                    bounds[i * 4 + 0] = static_cast<float>(instance.posX);
                    bounds[i * 4 + 1] = static_cast<float>(instance.posY);
                    bounds[i * 4 + 2] = static_cast<float>(instance.posZ);
                    bounds[i * 4 + 3] = kInstanceRadius;

                    bool visible = true;
                    for (int p = 0; p < 6 && visible; ++p)
                    {
                        const double dist = static_cast<double>(planes[p * 4 + 0]) * instance.posX +
                                            static_cast<double>(planes[p * 4 + 1]) * instance.posY +
                                            static_cast<double>(planes[p * 4 + 2]) * instance.posZ +
                                            static_cast<double>(planes[p * 4 + 3]);
                        if (dist < -static_cast<double>(kInstanceRadius)) visible = false;
                    }
                    if (visible) cpuVisible.push_back(i);
                }
                REQUIRE_FALSE(cpuVisible.empty());

                auto culling = GpuCulling::create(*allocator, {
                    .instanceCount = count, .framesInFlight = 1});
                REQUIRE(culling.has_value());
                REQUIRE(culling->uploadBounds(bounds).has_value());

                VkCommandPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
                poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                VkCommandPool pool = VK_NULL_HANDLE;
                REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &pool) == VK_SUCCESS);

                VkCommandBufferAllocateInfo cmdAlloc{};
                cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                cmdAlloc.commandPool = pool;
                cmdAlloc.commandBufferCount = 1;
                VkCommandBuffer cmd = VK_NULL_HANDLE;
                REQUIRE(vkAllocateCommandBuffers(device->device(), &cmdAlloc, &cmd) == VK_SUCCESS);

                VkFenceCreateInfo fenceInfo{};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                VkFence fence = VK_NULL_HANDLE;
                REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

                VkCommandBufferBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
                REQUIRE(culling->recordCulling(cmd, device->device(), *pipeline, 0,
                                               planes, count).has_value());
                REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);

                VkCommandBufferSubmitInfo submitCmd{};
                submitCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                submitCmd.commandBuffer = cmd;
                VkSubmitInfo2 submit{};
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
                submit.commandBufferInfoCount = 1;
                submit.pCommandBufferInfos = &submitCmd;
                REQUIRE(device->submit(1, &submit, fence) == VK_SUCCESS);
                REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

                auto indirectMapped = culling->indirectBuffer(0).map();
                REQUIRE(indirectMapped.has_value());
                (void)culling->indirectBuffer(0).invalidate(0, 16);
                const auto* command = static_cast<const std::uint32_t*>(*indirectMapped);
                const std::uint32_t gpuCount = command[1];
                culling->indirectBuffer(0).unmap();

                auto indicesMapped = culling->visibleIndicesBuffer(0).map();
                REQUIRE(indicesMapped.has_value());
                (void)culling->visibleIndicesBuffer(0).invalidate(0, count * sizeof(std::uint32_t));
                const auto* gpuIndicesRaw = static_cast<const std::uint32_t*>(*indicesMapped);
                std::vector<std::uint32_t> gpuIndices(gpuIndicesRaw, gpuIndicesRaw + gpuCount);
                culling->visibleIndicesBuffer(0).unmap();

                CHECK(gpuCount == cpuVisible.size());
                std::sort(gpuIndices.begin(), gpuIndices.end());
                CHECK(gpuIndices == cpuVisible); // bit-for-bit visible-set equivalence

                vkDestroyFence(device->device(), fence, nullptr);
                vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
                vkDestroyCommandPool(device->device(), pool, nullptr);
            }
        }
        CHECK(context->validationErrorCount() == 0);
    }
}
