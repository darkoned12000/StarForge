#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

namespace
{

constexpr std::uint32_t kW = 8;
constexpr std::uint32_t kH = 8;
constexpr VkDeviceSize kByteSize = kW * kH * 4;
constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;

} // namespace

TEST_CASE("Image RAII: layout tracking and RAII image view (2.1)")
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
            auto image = Image::create(*allocator, {
                .width = kW,
                .height = kH,
                .format = kFormat,
                .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc |
                         ImageUsageFlags::TransferDst,
            });
            REQUIRE(image.has_value());
            CHECK(image->currentLayout() == VK_IMAGE_LAYOUT_UNDEFINED);

            // RAII view: valid on creation, moved-from becomes invalid, and the
            // moved-to owner keeps a usable handle.
            auto view = image->createImageView(device->device());
            REQUIRE(view.has_value());
            CHECK(view->valid());
            CHECK(view->handle() != VK_NULL_HANDLE);
            Image::ImageView moved = std::move(*view);
            CHECK_FALSE(view->valid());
            CHECK(moved.valid());

            auto readback = Buffer::create(*allocator, {
                .size = kByteSize,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            VkCommandPool pool = VK_NULL_HANDLE;
            REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &pool) == VK_SUCCESS);

            VkCommandBufferAllocateInfo cmdAlloc{};
            cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAlloc.commandPool = pool;
            cmdAlloc.commandBufferCount = 1;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            REQUIRE(vkAllocateCommandBuffers(device->device(), &cmdAlloc, &cmd) == VK_SUCCESS);

            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);

            // First transition uses UNDEFINED as oldLayout (tracked state).
            image->transitionToColorAttachment(cmd);
            CHECK(image->currentLayout() == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            VkRenderingAttachmentInfo color{};
            color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color.imageView = moved.handle();
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue.color = VkClearColorValue{{1.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea.extent = {kW, kH};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            vkCmdBeginRendering(cmd, &rendering);
            vkCmdEndRendering(cmd);

            // Round-trip away and back. If the helper still assumed UNDEFINED,
            // the second color transition would discard the cleared pixels.
            image->transitionToTransferSrc(cmd);
            CHECK(image->currentLayout() == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            image->transitionToColorAttachment(cmd);
            CHECK(image->currentLayout() == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            image->transitionToTransferSrc(cmd);
            CHECK(image->currentLayout() == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {kW, kH, 1};
            vkCmdCopyImageToBuffer(cmd, image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback->handle(), 1, &region);

            VkBufferMemoryBarrier2 hostBarrier{};
            hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            hostBarrier.buffer = readback->handle();
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
            VkCommandBufferSubmitInfo submitCmd{};
            submitCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            submitCmd.commandBuffer = cmd;
            VkSubmitInfo2 submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &submitCmd;
            REQUIRE(device->submit(1, &submit, fence) == VK_SUCCESS);
            REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

            auto mapped = readback->map();
            REQUIRE(mapped.has_value());
            (void)readback->invalidate(0, kByteSize);
            const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
            CHECK(bytes[0] > 200); // red channel survived the layout round-trip
            CHECK(bytes[1] < 50);
            CHECK(bytes[2] < 50);
            readback->unmap();

            vkDestroyFence(device->device(), fence, nullptr);
            vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
            vkDestroyCommandPool(device->device(), pool, nullptr);
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
