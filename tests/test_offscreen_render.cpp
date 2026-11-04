#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_renderer.hpp"

#include <cstdint>
#include <cstring>

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

TEST_CASE("Offscreen render target: color + depth clear and readback")
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

        {
            constexpr std::uint32_t w = 32, h = 32;
            constexpr VkDeviceSize byteSize = w * h * 4;

            // Create color image (RGBA8 UNORM).
            auto colorImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .usage = ImageUsage::ColorAttachment,
            });
            REQUIRE(colorImage.has_value());

            auto colorView = colorImage->createView(device->device());
            REQUIRE(colorView.has_value());

            // Create depth image with format fallback.
            VkFormat depthFormat = findDepthFormat(context->physicalDevice());
            REQUIRE(depthFormat != VK_FORMAT_UNDEFINED);

            auto depthImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = depthFormat,
                .usage = ImageUsage::DepthAttachment,
            });
            REQUIRE(depthImage.has_value());

            auto depthView = depthImage->createView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
            REQUIRE(depthView.has_value());

            // Create readback buffer.
            auto readback = Buffer::create(*allocator, {
                .size = byteSize,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            // Create frame renderer (2 frames in flight, 1 swapchain image).
            auto renderer = FrameRenderer::create({
                .device = device->device(),
                .queueFamily = device->graphicsQueueFamily(),
                .framesInFlight = 2,
                .swapchainImageCount = 1,
            });
            REQUIRE(renderer.has_value());

            // Acquire fence.
            VkFence fence = renderer->currentFence();
            REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
            REQUIRE(vkResetFences(device->device(), 1, &fence) == VK_SUCCESS);

            VkCommandBuffer cmd = renderer->currentCommandBuffer();
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);

            // Transition color image to color attachment optimal.
            Image::transitionToColorAttachment(cmd, colorImage->handle());

            // Transition depth image to depth attachment optimal.
            Image::transitionToDepthAttachment(cmd, depthImage->handle(), depthFormat);

            // Dynamic rendering with clear.
            VkClearColorValue clearColor{{0.1f, 0.2f, 0.3f, 1.0f}};
            VkClearDepthStencilValue clearDepth{0.0f, 0};

            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = *colorView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue.color = clearColor;

            VkRenderingAttachmentInfo depthAttachment{};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = *depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil = clearDepth;

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = {0, 0};
            renderingInfo.renderArea.extent = {w, h};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;

            vkCmdBeginRendering(cmd, &renderingInfo);
            vkCmdEndRendering(cmd);

            // Transition color image to transfer src for readback.
            Image::transitionLayout(cmd, colorImage->handle(),
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

            // Copy image to buffer.
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {w, h, 1};
            vkCmdCopyImageToBuffer(cmd, colorImage->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  readback->handle(), 1, &region);

            // Barrier for host read.
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

            VkCommandBufferSubmitInfo cmdSubmit{};
            cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmdSubmit.commandBuffer = cmd;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &cmdSubmit;
            REQUIRE(vkQueueSubmit2(device->graphicsQueue(), 1, &submitInfo, fence) == VK_SUCCESS);
            REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

            // Read back and verify clear color.
            auto mapped = readback->map();
            REQUIRE(mapped.has_value());
            (void)readback->invalidate(0, byteSize);

            const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
            // RGBA8 UNORM: 0.1, 0.2, 0.3, 1.0 → 25, 51, 76, 255 (approx)
            bool r0 = bytes[0] >= 24 && bytes[0] <= 27;
            bool r1 = bytes[1] >= 50 && bytes[1] <= 52;
            bool r2 = bytes[2] >= 75 && bytes[2] <= 77;
            CHECK(r0);
            CHECK(r1);
            CHECK(r2);
            CHECK(bytes[3] == 255);

            readback->unmap();

            vkDestroyImageView(device->device(), *colorView, nullptr);
            vkDestroyImageView(device->device(), *depthView, nullptr);
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
