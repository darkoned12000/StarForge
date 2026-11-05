#include <doctest/doctest.h>

#include "bench/benchmark_scene.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "gpu_resources/gpu_culling.hpp"
#include "gpu_resources/transfer.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_timestamps.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::bench;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;
using namespace starforge::math;

TEST_CASE("GPU-driven: instance culling timing at scale")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        Allocator::CreateInfo allocInfo;
        allocInfo.instance = context->instance();
        allocInfo.physicalDevice = context->physicalDevice();
        allocInfo.device = device->device();
        auto allocator = Allocator::create(allocInfo);
        REQUIRE(allocator.has_value());

        const std::string cullPath = std::string(STARFORGE_SHADER_DIR) + "/cull.spv";
        std::ifstream cullFile(cullPath, std::ios::ate | std::ios::binary);
        REQUIRE(cullFile.is_open());
        const auto cullSize = static_cast<std::size_t>(cullFile.tellg());
        std::vector<std::uint8_t> cullBytes(cullSize);
        cullFile.seekg(0);
        cullFile.read(reinterpret_cast<char*>(cullBytes.data()), static_cast<std::streamsize>(cullSize));

        // GPU timer for culling duration.
        GpuTimer::CreateInfo timerInfo;
        timerInfo.device = device->device();
        timerInfo.physicalDevice = context->physicalDevice();
        timerInfo.queueFamily = device->graphicsQueueFamily();
        timerInfo.framesInFlight = 1;
        timerInfo.maxScopesPerFrame = 4;
        auto timer = GpuTimer::create(timerInfo);
        REQUIRE(timer.has_value());

        const std::uint32_t counts[] = {10000, 100000};

        for (std::uint32_t ic : counts)
        {
            // Generate scene.
            auto scene = generateScene(42, ic, 10.0);

            // Prepare bounds data.
            std::vector<float> boundsData(ic * 4);
            for (std::uint32_t i = 0; i < ic; ++i)
            {
                boundsData[i * 4 + 0] = static_cast<float>(scene.instances[i].posX);
                boundsData[i * 4 + 1] = static_cast<float>(scene.instances[i].posY);
                boundsData[i * 4 + 2] = static_cast<float>(scene.instances[i].posZ);
                boundsData[i * 4 + 3] = 0.5f * scene.instances[i].scale;
            }

            // Create GpuCulling.
            auto gpuCulling = GpuCulling::create(*allocator, {
                .instanceCount = ic,
                .framesInFlight = 1,
            });
            REQUIRE(gpuCulling.has_value());

            REQUIRE(gpuCulling->uploadBounds(boundsData).has_value());

            // Create culling compute pipeline.
            auto cullPipeline = ComputePipeline::create(*device, {
                .spirv = cullBytes,
                .entryPoint = "main",
                .storageBufferCount = 3,
                .pushConstantSize = 104,
            });
            REQUIRE(cullPipeline.has_value());

            // Frustum planes.
            Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
            Mat4d proj = glm::perspective(1.0, 16.0 / 9.0, 100.0, 0.1);
            Mat4d vp = proj * view;
            Frustum frustum = buildFrustum(vp);

            float planes[24];
            for (int p = 0; p < 6; ++p)
            {
                for (int c = 0; c < 4; ++c)
                {
                    planes[p * 4 + c] = static_cast<float>(frustum.planes[p][c]);
                }
            }

            // Create command pool.
            VkCommandPool cmdPool = VK_NULL_HANDLE;
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &cmdPool) == VK_SUCCESS);

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.commandPool = cmdPool;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandBufferCount = 1;
            REQUIRE(vkAllocateCommandBuffers(device->device(), &cmdAllocInfo, &cmd) == VK_SUCCESS);

            // Reset indirect buffer.
            REQUIRE(gpuCulling->resetIndirectBuffer(0).has_value());

            // Record culling.
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);

            timer->resetQueryPool(cmd, 0, 2);
            timer->beginScope(cmd, 0, "cull");

            auto recordResult = gpuCulling->recordCulling(
                cmd, device->device(), *cullPipeline, 0, planes, ic);
            REQUIRE(recordResult.has_value());

            timer->endScope(cmd, 0);
            REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);

            // Submit and wait.
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence = VK_NULL_HANDLE;
            REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

            VkCommandBufferSubmitInfo cmdSubmit{};
            cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmdSubmit.commandBuffer = cmd;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &cmdSubmit;

            REQUIRE(vkQueueSubmit2(device->graphicsQueue(), 1, &submitInfo, fence) == VK_SUCCESS);
            REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

            // Collect timing.
            (void)timer->collectResults(0);

            // Read back visible count.
            auto& indirectBuf = gpuCulling->indirectBuffer(0);
            auto mapped = indirectBuf.map();
            REQUIRE(mapped.has_value());
            (void)indirectBuf.invalidate(0, 16);
            auto* data = static_cast<std::uint32_t*>(*mapped);
            std::uint32_t visibleCount = data[1];
            indirectBuf.unmap();

            // Verify.
            CHECK(visibleCount > 0);
            CHECK(visibleCount <= ic);

            // Cleanup.
            vkDestroyFence(device->device(), fence, nullptr);
            vkDestroyCommandPool(device->device(), cmdPool, nullptr);
        }

        CHECK(context->validationErrorCount() == 0);
    }
}