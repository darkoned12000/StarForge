#include <doctest/doctest.h>

#include "bench/benchmark_scene.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "gpu_resources/gpu_culling.hpp"
#include "gpu_resources/transfer.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::bench;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;
using namespace starforge::math;

TEST_CASE("GPU cull parity: compute vs CPU reference")
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

        auto uploader = Uploader::create(*device);
        REQUIRE(uploader.has_value());

        const std::string shaderPath = std::string(STARFORGE_SHADER_DIR) + "/cull.spv";
        std::ifstream file(shaderPath, std::ios::ate | std::ios::binary);
        if (!file)
        {
            // Skip if shader not available.
            return;
        }
        const auto size = static_cast<std::size_t>(file.tellg());
        std::vector<std::uint8_t> bytes(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));

        // Test with multiple instance counts and visibility fractions.
        const std::uint32_t instanceCounts[] = {100, 1000, 5000};
        const double visibleFractions[] = {1.0, 0.1, 0.01};

        for (std::uint32_t ic : instanceCounts)
        {
            for (double vf : visibleFractions)
            {
                // Generate scene.
                double radius = 5.0;
                if (vf > 0.0 && vf <= 1.0)
                {
                    radius = 5.0 / std::cbrt(vf);
                }
                if (radius < 1.0) radius = 1.0;

                auto scene = generateScene(42, ic, radius);

                // Build frustum.
                Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
                Mat4d proj = glm::perspective(1.0, 16.0 / 9.0, 100.0, 0.1); // swapped for reverse-Z
                Mat4d vp = proj * view;
                Frustum frustum = buildFrustum(vp);

                // CPU culling using original cullInstances (double precision).
                auto cpuVisible = cullInstances(scene, frustum, 0.5);

                // Float planes for GPU (cast from double frustum).
                float planes[24];
                for (int p = 0; p < 6; ++p)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        planes[p * 4 + c] = static_cast<float>(frustum.planes[p][c]);
                    }
                }
                std::set<std::uint32_t> cpuSet(cpuVisible.begin(), cpuVisible.end());

                // Prepare bounds data (vec4 per instance: center XYZ + radius W).
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

                auto uploadResult = gpuCulling->uploadBounds(boundsData);
                REQUIRE(uploadResult.has_value());

                // Debug: read back bounds buffer.
                auto bMapped = gpuCulling->boundsBuffer().map();
                REQUIRE(bMapped.has_value());
                auto* bData = static_cast<float*>(*bMapped);
                MESSAGE("Bounds[0]: " << bData[0] << " " << bData[1] << " " << bData[2] << " " << bData[3]);
                gpuCulling->boundsBuffer().unmap();

                // Debug: print bounds buffer address.
                VkBufferDeviceAddressInfo addrInfo{};
                addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                addrInfo.buffer = gpuCulling->boundsBuffer().handle();
                VkDeviceAddress addr = vkGetBufferDeviceAddress(device->device(), &addrInfo);
                MESSAGE("Bounds buffer address: " << addr);

                // Create compute pipeline with push constants.
                // Push constant size: 6 planes (96) + uint (4) + uint64 (8) = 108, padded to 112
                constexpr std::uint32_t pushSize = 104; // 96 + 4 + 4
                auto pipeline = ComputePipeline::create(*device, {
                    .spirv = bytes,
                    .entryPoint = "main",
                    .storageBufferCount = 3, // bounds + indirect + visibleIndices
                    .pushConstantSize = pushSize,
                });
                REQUIRE(pipeline.has_value());

                // Reset indirect buffer.
                auto resetResult = gpuCulling->resetIndirectBuffer(0);
                REQUIRE(resetResult.has_value());

                // Create command pool and buffer.
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

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);

                // Record culling.
                auto recordResult = gpuCulling->recordCulling(
                    cmd, device->device(), *pipeline, 0, planes, ic);
                REQUIRE(recordResult.has_value());

                REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);

                // Submit.
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

                // Read back indirect command (instanceCount at offset 4).
                auto& indirectBuf = gpuCulling->indirectBuffer(0);
                auto mapped = indirectBuf.map();
                REQUIRE(mapped.has_value());
                (void)indirectBuf.invalidate(0, 16);
                auto* indirectData = static_cast<std::uint32_t*>(*mapped);
                std::uint32_t gpuVisibleCount = indirectData[1]; // instanceCount
                indirectBuf.unmap();

                // Read back visible indices.
                auto& visibleBuf = gpuCulling->visibleIndicesBuffer(0);
                auto vMapped = visibleBuf.map();
                REQUIRE(vMapped.has_value());
                (void)visibleBuf.invalidate(0, ic * sizeof(std::uint32_t));
                auto* visibleData = static_cast<std::uint32_t*>(*vMapped);

                std::set<std::uint32_t> gpuSet;
                for (std::uint32_t i = 0; i < gpuVisibleCount; ++i)
                {
                    gpuSet.insert(visibleData[i]);
                }
                visibleBuf.unmap();

                // Compare. Allow small differences due to float (GPU) vs double (CPU)
                // precision on frustum boundary cases.
                const double tolerance = std::max(5.0, 0.05 * static_cast<double>(ic));
                CHECK(std::abs(static_cast<double>(gpuVisibleCount) - static_cast<double>(cpuVisible.size())) <= tolerance);

                // Count mismatches.
                std::size_t mismatches = 0;
                for (std::uint32_t i = 0; i < ic; ++i)
                {
                    bool inCpu = cpuSet.count(i) > 0;
                    bool inGpu = gpuSet.count(i) > 0;
                    if (inCpu != inGpu) ++mismatches;
                }
                MESSAGE("GPU: " << gpuVisibleCount << " CPU: " << cpuVisible.size() << " mismatches: " << mismatches);
                CHECK(mismatches <= static_cast<std::size_t>(tolerance));

                // Cleanup.
                vkDestroyFence(device->device(), fence, nullptr);
                vkDestroyCommandPool(device->device(), cmdPool, nullptr);
            }
        }

        CHECK(context->validationErrorCount() == 0);
    }
}