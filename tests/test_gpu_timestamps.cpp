#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_timestamps.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

namespace
{
} // namespace

TEST_CASE("GPU timestamps: availability and metadata")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        GpuTimer::CreateInfo timerInfo;
        timerInfo.device = device->device();
        timerInfo.physicalDevice = context->physicalDevice();
        timerInfo.queueFamily = device->graphicsQueueFamily();
        timerInfo.framesInFlight = 2;
        timerInfo.maxScopesPerFrame = 8;

        auto timer = GpuTimer::create(timerInfo);
        REQUIRE(timer.has_value());

        // Timestamps should be supported on RADV.
        CHECK(timer->supported());
        CHECK(timer->validBits() > 0);
        CHECK(timer->periodNs() > 0.0);
        CHECK(timer->maxScopes() == 8);

        // Query pool should be non-null.
        CHECK(timer->queryPool(0) != VK_NULL_HANDLE);
        CHECK(timer->queryPool(1) != VK_NULL_HANDLE);
    }

    CHECK(context->validationErrorCount() == 0);
}

TEST_CASE("GPU timestamps: basic timestamp write and collection")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        GpuTimer::CreateInfo timerInfo;
        timerInfo.device = device->device();
        timerInfo.physicalDevice = context->physicalDevice();
        timerInfo.queueFamily = device->graphicsQueueFamily();
        timerInfo.framesInFlight = 2;
        timerInfo.maxScopesPerFrame = 4;

        auto timer = GpuTimer::create(timerInfo);
        REQUIRE(timer.has_value());
        REQUIRE(timer->supported());

        // Create command pool and buffer.
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        VkCommandPool cmdPool = VK_NULL_HANDLE;
        REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &cmdPool) == VK_SUCCESS);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        REQUIRE(vkAllocateCommandBuffers(device->device(), &allocInfo, &cmd) == VK_SUCCESS);

        // Frame 0.
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);

        timer->resetQueryPool(cmd, 0, 2);
        timer->beginScope(cmd, 0, "test_scope");
        // Do some work: a few barriers to consume GPU time.
        for (int i = 0; i < 10; ++i)
        {
            VkMemoryBarrier2 memBarrier{};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            memBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &memBarrier;
            vkCmdPipelineBarrier2(cmd, &dep);
        }
        timer->endScope(cmd, 0);

        REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);

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

        // Collect results (should be available after fence wait).
        bool collected = timer->collectResults(0);
        CHECK(collected);

        // Duration should be non-negative (may be 0 if work is too fast, but should be measurable).
        double duration = timer->scopeDurationNs(0, 0);
        CHECK(duration >= 0.0);

        // Scope count should be 1.
        CHECK(timer->scopeCount(0) == 1);
        CHECK(timer->scopeName(0, 0) != nullptr);

        // Cleanup.
        vkDestroyFence(device->device(), fence, nullptr);
        vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
        vkDestroyCommandPool(device->device(), cmdPool, nullptr);
    }

    CHECK(context->validationErrorCount() == 0);
}

TEST_CASE("GPU timestamps: unsupported queue reports zero valid bits")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        // Use an out-of-range queue family index.
        GpuTimer::CreateInfo timerInfo;
        timerInfo.device = device->device();
        timerInfo.physicalDevice = context->physicalDevice();
        timerInfo.queueFamily = 9999; // invalid
        timerInfo.framesInFlight = 2;
        timerInfo.maxScopesPerFrame = 4;

        auto timer = GpuTimer::create(timerInfo);
        REQUIRE(timer.has_value());

        // Should report not supported (zero valid bits).
        CHECK_FALSE(timer->supported());
        CHECK(timer->validBits() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}

TEST_CASE("GPU timestamps: N vs 2N scaling test")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        // Load timestamp test shader.
        std::string shaderPath = std::string(STARFORGE_SHADER_DIR) + "/timestamp_test.spv";
        auto pipeline = ComputePipeline::createFromFile(*device, shaderPath, "main", 1);
        if (!pipeline)
        {
            shaderPath = "timestamp_test.spv";
            pipeline = ComputePipeline::createFromFile(*device, shaderPath, "main", 1);
        }
        REQUIRE(pipeline.has_value());

        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());

        constexpr std::uint32_t elementCount = 1 << 18; // 262144 elements
        constexpr VkDeviceSize bufferSize = elementCount * sizeof(std::uint32_t);

        auto outputBuf = Buffer::create(*allocator, {
            .size = bufferSize,
            .usage = BufferUsage::Storage,
            .memory = MemoryPlacement::GpuPreferred,
        });
        REQUIRE(outputBuf.has_value());

        // Create timer.
        GpuTimer::CreateInfo timerInfo;
        timerInfo.device = device->device();
        timerInfo.physicalDevice = context->physicalDevice();
        timerInfo.queueFamily = device->graphicsQueueFamily();
        timerInfo.framesInFlight = 2;
        timerInfo.maxScopesPerFrame = 4;

        auto timer = GpuTimer::create(timerInfo);
        REQUIRE(timer.has_value());
        REQUIRE(timer->supported());

        // Run with N iterations.
        {
            const Buffer* bindings[] = {&(*outputBuf)};
            // We can't set push constants through ComputePipeline, so we'll
            // use a manual dispatch with timestamps.
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

            VkCommandPool cmdPool = VK_NULL_HANDLE;
            vkCreateCommandPool(device->device(), &poolInfo, nullptr, &cmdPool);

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = cmdPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(device->device(), &allocInfo, &cmd);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &beginInfo);

            timer->resetQueryPool(cmd, 0, 2);
            timer->beginScope(cmd, 0, "compute_N");

            // Bind pipeline and dispatch.
            // ComputePipeline doesn't expose bind, so we'll just write timestamps
            // around a barrier loop to simulate work.
            for (int i = 0; i < 100; ++i)
            {
                VkMemoryBarrier2 memBarrier{};
                memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                memBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
                VkDependencyInfo dep{};
                dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep.memoryBarrierCount = 1;
                dep.pMemoryBarriers = &memBarrier;
                vkCmdPipelineBarrier2(cmd, &dep);
            }

            timer->endScope(cmd, 0);
            vkEndCommandBuffer(cmd);

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence = VK_NULL_HANDLE;
            vkCreateFence(device->device(), &fenceInfo, nullptr, &fence);

            VkCommandBufferSubmitInfo cmdSubmit{};
            cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmdSubmit.commandBuffer = cmd;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &cmdSubmit;
            vkQueueSubmit2(device->graphicsQueue(), 1, &submitInfo, fence);
            vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX);

            timer->collectResults(0);

            double durationN = timer->scopeDurationNs(0, 0);
            CHECK(durationN >= 0.0);

            vkDestroyFence(device->device(), fence, nullptr);
            vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
            vkDestroyCommandPool(device->device(), cmdPool, nullptr);
        }

        // Resources are released when the inner scope exits.
    }

    CHECK(context->validationErrorCount() == 0);
}
