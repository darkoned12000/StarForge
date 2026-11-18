#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_timestamps.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
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

TEST_CASE("GPU timestamps: real N vs 2N dispatch, frame recycling, nested scopes (2.3)")
{
    struct PushConstants
    {
        std::uint32_t iterations = 0;
        std::uint32_t pad0 = 0;
        std::uint32_t pad1 = 0;
        std::uint32_t pad2 = 0;
    };

    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        auto pipeline = ComputePipeline::createFromFile(
            *device, std::string(STARFORGE_SHADER_DIR) + "/timestamp_scaling.spv",
            "main", 1, static_cast<std::uint32_t>(sizeof(PushConstants)));
        REQUIRE(pipeline.has_value());

        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());

        constexpr std::uint32_t kElementCount = 1 << 16;
        constexpr VkDeviceSize kBufferSize = kElementCount * sizeof(std::uint32_t);
        auto output = Buffer::create(*allocator, {
            .size = kBufferSize, .usage = BufferUsage::Storage, .memory = MemoryPlacement::GpuPreferred});
        REQUIRE(output.has_value());

        GpuTimer::CreateInfo timerInfo;
        timerInfo.device = device->device();
        timerInfo.physicalDevice = context->physicalDevice();
        timerInfo.queueFamily = device->graphicsQueueFamily();
        timerInfo.framesInFlight = 2;
        timerInfo.maxScopesPerFrame = 4;
        auto timer = GpuTimer::create(timerInfo);
        REQUIRE(timer.has_value());
        REQUIRE(timer->supported());

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool pool = VK_NULL_HANDLE;
        REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &pool) == VK_SUCCESS);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool;
        allocInfo.commandBufferCount = 2; // one per frame slot
        VkCommandBuffer cmds[2] = {};
        REQUIRE(vkAllocateCommandBuffers(device->device(), &allocInfo, cmds) == VK_SUCCESS);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

        auto measure = [&](std::uint32_t frameIndex, std::uint32_t iterations, double& parentNs,
                           double& childNs) {
            const PushConstants push{iterations, 0, 0, 0};
            const Buffer* bindings[] = {&(*output)};
            VkCommandBuffer cmd = cmds[frameIndex];

            REQUIRE(vkResetFences(device->device(), 1, &fence) == VK_SUCCESS);
            REQUIRE(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);

            timer->resetQueryPool(cmd, frameIndex, 4);
            timer->beginScope(cmd, frameIndex, "workload"); // outer: queries 0,1
            timer->beginScope(cmd, frameIndex, "dispatch"); // inner: queries 2,3

            VkBufferMemoryBarrier2 pre{};
            pre.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            pre.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            pre.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            pre.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            pre.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            pre.buffer = output->handle();
            pre.size = VK_WHOLE_SIZE;
            VkDependencyInfo preDep{};
            preDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            preDep.bufferMemoryBarrierCount = 1;
            preDep.pBufferMemoryBarriers = &pre;
            vkCmdPipelineBarrier2(cmd, &preDep);

            REQUIRE(pipeline->recordDispatch(cmd, bindings, &push, sizeof(push),
                                             kElementCount / 64).has_value());

            VkBufferMemoryBarrier2 post{};
            post.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            post.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            post.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            post.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            post.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            post.buffer = output->handle();
            post.size = VK_WHOLE_SIZE;
            VkDependencyInfo postDep{};
            postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            postDep.bufferMemoryBarrierCount = 1;
            postDep.pBufferMemoryBarriers = &post;
            vkCmdPipelineBarrier2(cmd, &postDep);

            timer->endScope(cmd, frameIndex); // inner
            timer->endScope(cmd, frameIndex); // outer
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

            // Some drivers report VK_NOT_READY once for freshly-reset nested
            // queries; retry briefly rather than treating that as a failure.
            bool collected = false;
            for (int attempt = 0; attempt < 100 && !collected; ++attempt)
            {
                collected = timer->collectResults(frameIndex);
                if (!collected) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE(collected);
            CHECK(timer->scopeCount(frameIndex) == 2);
            parentNs = timer->scopeDurationNs(frameIndex, 0);
            childNs = timer->scopeDurationNs(frameIndex, 1);
        };

        double nFrame0 = 0.0, nChild0 = 0.0;
        double nFrame1 = 0.0, nChild1 = 0.0;
        double dFrame0 = 0.0, dChild0 = 0.0;
        double dFrame1 = 0.0, dChild1 = 0.0;
        measure(0, 256, nFrame0, nChild0);
        measure(1, 256, nFrame1, nChild1);
        measure(0, 512, dFrame0, dChild0);
        measure(1, 512, dFrame1, dChild1);

        // Both frame slots must produce valid, non-zero timings (frame-1 recycling).
        CHECK(nFrame0 > 0.0);
        CHECK(nFrame1 > 0.0);
        CHECK(dFrame0 > 0.0);
        CHECK(dFrame1 > 0.0);

        // Nested child scopes are contained within their parent.
        CHECK(nChild0 > 0.0);
        CHECK(nChild1 > 0.0);
        CHECK(nChild0 <= nFrame0);
        CHECK(nChild1 <= nFrame1);
        CHECK(dChild0 <= dFrame0);
        CHECK(dChild1 <= dFrame1);

        // Twice the compute iterations must take longer (median of two slots).
        CHECK(std::min(dFrame0, dFrame1) > std::min(nFrame0, nFrame1));

        vkDestroyFence(device->device(), fence, nullptr);
        vkFreeCommandBuffers(device->device(), pool, 2, cmds);
        vkDestroyCommandPool(device->device(), pool, nullptr);
    }

    CHECK(context->validationErrorCount() == 0);
}
