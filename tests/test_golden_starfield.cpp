#include <doctest/doctest.h>

// stb PNG IO (single implementation TU for the gpu test binary).
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "environment/starfield_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <chrono>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif
#ifndef STARFORGE_GOLDEN_DIR
#define STARFORGE_GOLDEN_DIR "tests/goldens"
#endif

using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

namespace
{

constexpr std::uint32_t kW = 128;
constexpr std::uint32_t kH = 128;
constexpr int kPerChannelTolerance = 8;

} // namespace

TEST_CASE("Golden: starfield matches reference image (8.8)")
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

        auto colorImage = Image::create(*allocator, {
            .width = kW,
            .height = kH,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = ImageUsageFlags::ColorAttachment,
        });
        REQUIRE(colorImage.has_value());
        auto colorView = colorImage->createView(device->device());
        REQUIRE(colorView.has_value());

        auto readback = Buffer::create(*allocator, {
            .size = kW * kH * 4,
            .usage = BufferUsage::TransferDst,
            .memory = MemoryPlacement::GpuToCpu,
        });
        REQUIRE(readback.has_value());

        auto starRenderer = StarfieldRenderer::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
        });
        REQUIRE(starRenderer.has_value());

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

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);

        Image::transitionToColorAttachment(cmd, colorImage->handle());

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = *colorView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = {kW, kH};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = static_cast<float>(kH);
        viewport.width = static_cast<float>(kW);
        viewport.height = -static_cast<float>(kH);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {kW, kH};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        StarfieldRenderContext starContext;
        starContext.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        starContext.time = 1.0f;
        StarfieldParams starParams;
        starParams.time = 1.0f;
        starRenderer->render(cmd, starContext, starParams);

        vkCmdEndRendering(cmd);

        Image::transitionLayout(cmd, colorImage->handle(),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {kW, kH, 1};
        vkCmdCopyImageToBuffer(cmd, colorImage->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback->handle(), 1, &region);

        VkBufferMemoryBarrier2 hostBarrier{};
        hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = readback->handle();
        hostBarrier.offset = 0;
        hostBarrier.size = VK_WHOLE_SIZE;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &hostBarrier;
        vkCmdPipelineBarrier2(cmd, &dep);

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

        auto mapped = readback->map();
        REQUIRE(mapped.has_value());
        (void)readback->invalidate(0, kW * kH * 4);
        const auto* actual = static_cast<const std::uint8_t*>(*mapped);

        const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/starfield_128.png";
        const bool updateMode = std::getenv("STARFORGE_UPDATE_GOLDENS") != nullptr;

        if (updateMode)
        {
            REQUIRE(stbi_write_png(goldenPath.c_str(), static_cast<int>(kW), static_cast<int>(kH), 4, actual,
                                   static_cast<int>(kW * 4)) == 1);
        }
        else
        {
            int gw = 0, gh = 0, gcomp = 0;
            unsigned char* golden = stbi_load(goldenPath.c_str(), &gw, &gh, &gcomp, 4);
            REQUIRE_MESSAGE(golden != nullptr, "Golden file missing: " << goldenPath);
            REQUIRE(gw == static_cast<int>(kW));
            REQUIRE(gh == static_cast<int>(kH));

            std::uint32_t failing = 0;
            for (std::uint32_t i = 0; i < kW * kH * 4; ++i)
            {
                int diff = static_cast<int>(actual[i]) - static_cast<int>(golden[i]);
                if (std::abs(diff) > kPerChannelTolerance)
                {
                    ++failing;
                }
            }
            stbi_image_free(golden);
            CHECK(static_cast<double>(failing) / static_cast<double>(kW * kH * 4) < 0.01);
        }
        readback->unmap();

        vkDestroyFence(device->device(), fence, nullptr);
        vkDestroyImageView(device->device(), *colorView, nullptr);
        vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
        vkDestroyCommandPool(device->device(), cmdPool, nullptr);

        colorImage = std::expected<Image, std::string>();
        readback = std::expected<Buffer, std::string>();
    }

    CHECK(context->validationErrorCount() == 0);
}

TEST_CASE("Budget: starfield frame time under ADR-004 interval (8.8)")
{
    // Times 30 real frames (clear + starfield, submit to fence signal) at
    // test resolution. ADR-004 budgets target 1080p on the reference GPU
    // via the M4 benchmark harness; this guards the environment render
    // path against pathological per-frame regressions.
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

        auto colorImage = Image::create(*allocator, {
            .width = kW,
            .height = kH,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = ImageUsageFlags::ColorAttachment,
        });
        REQUIRE(colorImage.has_value());
        auto colorView = colorImage->createView(device->device());
        REQUIRE(colorView.has_value());

        auto starRenderer = StarfieldRenderer::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
        });
        REQUIRE(starRenderer.has_value());

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool cmdPool = VK_NULL_HANDLE;
        REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &cmdPool) == VK_SUCCESS);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        REQUIRE(vkAllocateCommandBuffers(device->device(), &allocInfo, &cmd) == VK_SUCCESS);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

        StarfieldRenderContext starContext;
        starContext.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        StarfieldParams starParams;

        constexpr int kFrames = 30;
        std::vector<double> timings; // Post-warm-up frame times (ms).
        timings.reserve(kFrames);
        for (int frame = 0; frame < kFrames; ++frame)
        {
            starContext.time = static_cast<float>(frame) * 0.016f;
            starParams.time = starContext.time;

            vkResetCommandBuffer(cmd, 0);
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &beginInfo);

            Image::transitionToColorAttachment(cmd, colorImage->handle());
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = *colorView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = {0, 0};
            renderingInfo.renderArea.extent = {kW, kH};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            vkCmdBeginRendering(cmd, &renderingInfo);

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = static_cast<float>(kH);
            viewport.width = static_cast<float>(kW);
            viewport.height = -static_cast<float>(kH);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = {kW, kH};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            starRenderer->render(cmd, starContext, starParams);
            vkCmdEndRendering(cmd);
            vkEndCommandBuffer(cmd);

            const auto frameStart = std::chrono::steady_clock::now();
            VkCommandBufferSubmitInfo cmdSubmit{};
            cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmdSubmit.commandBuffer = cmd;
            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &cmdSubmit;
            vkQueueSubmit2(device->graphicsQueue(), 1, &submitInfo, fence);
            vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX);
            const auto frameEnd = std::chrono::steady_clock::now();
            vkResetFences(device->device(), 1, &fence);
            const double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
            if (frame >= 3) // Skip warm-up frames (JIT, allocator growth).
            {
                timings.push_back(frameMs);
            }
        }

        std::sort(timings.begin(), timings.end());
        const double p95 = timings[(timings.size() * 95) / 100];
        // ADR-004 interval is 6.94 ms at 1080p; at 128x128 the render
        // path must stay far under it (guards driver/path regressions).
        CHECK(p95 < 6.94);

        vkDestroyFence(device->device(), fence, nullptr);
        vkDestroyImageView(device->device(), *colorView, nullptr);
        vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
        vkDestroyCommandPool(device->device(), cmdPool, nullptr);
        colorImage = std::expected<Image, std::string>();
    }

    CHECK(context->validationErrorCount() == 0);
}
