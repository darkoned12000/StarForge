#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

TEST_CASE("Image: allocate, transition, and read back a 4x4 RGBA8 image")
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
            constexpr std::uint32_t w = 4, h = 4;
            auto image = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .usage = ImageUsage::ColorAttachment,
            });
            REQUIRE(image.has_value());
            CHECK(image->width() == w);
            CHECK(image->height() == h);
            CHECK(image->format() == VK_FORMAT_R8G8B8A8_UNORM);
            CHECK(image->aspectMask() == VK_IMAGE_ASPECT_COLOR_BIT);

            auto view = image->createView(device->device());
            REQUIRE(view.has_value());
            CHECK(*view != VK_NULL_HANDLE);

            // Create a readback buffer.
            constexpr VkDeviceSize bufferSize = w * h * 4;
            auto readback = Buffer::create(*allocator, {
                .size = bufferSize,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            // Record commands: transition to color attachment, clear, transition to transfer src, copy to buffer.
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

            // Transition to transfer dst optimal for clear.
            Image::transitionToTransferDst(cmd, image->handle());

            // Clear the image.
            VkClearColorValue clearColor{{0.25f, 0.5f, 0.75f, 1.0f}};
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            vkCmdClearColorImage(cmd, image->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &clearColor, 1, &range);

            // Transition from transfer dst to transfer src for readback.
            Image::transitionLayout(cmd, image->handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

            // Copy image to buffer.
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {w, h, 1};
            vkCmdCopyImageToBuffer(cmd, image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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

            // Read back and verify.
            auto mapped = readback->map();
            REQUIRE(mapped.has_value());
            readback->invalidate(0, bufferSize);

            const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
            // RGBA8 UNORM: 0.25, 0.5, 0.75, 1.0 → 64, 128, 191, 255
            CHECK(bytes[0] == 64);
            CHECK(bytes[1] == 128);
            CHECK(bytes[2] == 191);
            CHECK(bytes[3] == 255);

            readback->unmap();

            // Cleanup.
            vkDestroyFence(device->device(), fence, nullptr);
            vkDestroyImageView(device->device(), *view, nullptr);
            vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
            vkDestroyCommandPool(device->device(), cmdPool, nullptr);
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
