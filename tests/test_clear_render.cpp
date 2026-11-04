#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_renderer.hpp"

#include <cstdint>
#include <cstring>

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

namespace
{

struct OffscreenImage
{
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    ~OffscreenImage()
    {
        if (device != VK_NULL_HANDLE && view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, view, nullptr);
        }
        if (allocator != VK_NULL_HANDLE && image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(allocator, image, allocation);
        }
    }
};

} // namespace

TEST_CASE("Dynamic rendering clear: offscreen color attachment clear and readback")
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
            constexpr std::uint32_t width = 16;
            constexpr std::uint32_t height = 16;
            constexpr VkDeviceSize pixelCount = width * height;
            constexpr VkDeviceSize byteSize = pixelCount * 4;

            OffscreenImage target;
            target.allocator = allocator->handle();
            target.device = device->device();

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.extent = {width, height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo vmaInfo{};
            vmaInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

            REQUIRE(vmaCreateImage(allocator->handle(), &imageInfo, &vmaInfo, &target.image, &target.allocation, nullptr) == VK_SUCCESS);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = target.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            REQUIRE(vkCreateImageView(device->device(), &viewInfo, nullptr, &target.view) == VK_SUCCESS);

            auto readback = Buffer::create(*allocator, {
                .size = byteSize,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            auto renderer = FrameRenderer::create({
                .device = device->device(),
                .queueFamily = device->graphicsQueueFamily(),
                .framesInFlight = 2,
                .swapchainImageCount = 2,
            });
            REQUIRE(renderer.has_value());

            VkCommandBuffer cmd = renderer->currentCommandBuffer();
            VkFence fence = renderer->currentFence();
            REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
            REQUIRE(vkResetFences(device->device(), 1, &fence) == VK_SUCCESS);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);

            const VkClearColorValue clearColor{{1.0f, 0.0f, 0.0f, 1.0f}};
            recordClearColorImage(
                cmd,
                target.image,
                target.view,
                {width, height},
                clearColor,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->handle(), 1, &region);

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

            auto mapped = readback->map();
            REQUIRE(mapped.has_value());
            (void)readback->invalidate(0, byteSize);

            const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
            bool allMatch = true;
            for (std::size_t i = 0; i < pixelCount; ++i)
            {
                if (bytes[i * 4 + 0] != 255 ||
                    bytes[i * 4 + 1] != 0 ||
                    bytes[i * 4 + 2] != 0 ||
                    bytes[i * 4 + 3] != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            CHECK(allMatch);
            readback->unmap();
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
