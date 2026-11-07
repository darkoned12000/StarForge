#include <doctest/doctest.h>

#include "environment/starfield_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

TEST_CASE("Starfield render: procedural stars appear in offscreen image")
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
            constexpr std::uint32_t w = 128, h = 128;
            constexpr VkDeviceSize byteSize = w * h * 4;

            auto colorImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .usage = ImageUsageFlags::ColorAttachment,
            });
            REQUIRE(colorImage.has_value());

            auto colorView = colorImage->createView(device->device());
            REQUIRE(colorView.has_value());

            auto readback = Buffer::create(*allocator, {
                .size = byteSize,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            auto starRenderer = StarfieldRenderer::create({
                .device = device->device(),
                .physicalDevice = context->physicalDevice(),
                .queueFamily = device->graphicsQueueFamily(),
                .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
            });
            REQUIRE(starRenderer.has_value());

            {
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
                renderingInfo.renderArea.extent = {w, h};
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachments = &colorAttachment;

                vkCmdBeginRendering(cmd, &renderingInfo);

                VkViewport viewport{};
                viewport.x = 0.0f;
                viewport.y = static_cast<float>(h);
                viewport.width = static_cast<float>(w);
                viewport.height = -static_cast<float>(h);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor{};
                scissor.offset = {0, 0};
                scissor.extent = {w, h};
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                StarfieldRenderContext starContext;
                starContext.resolution = {static_cast<float>(w), static_cast<float>(h)};
                starContext.time = 1.0f;
                StarfieldParams starParams;
                starParams.time = 1.0f;
                starParams.intensity = 1.0f;

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
                region.imageExtent = {w, h, 1};
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
                (void)readback->invalidate(0, byteSize);

                const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
                std::uint32_t brightPixels = 0;
                std::uint32_t darkPixels = 0;
                for (std::size_t i = 0; i < byteSize; i += 4)
                {
                    const auto luminance =
                        static_cast<std::uint32_t>(bytes[i]) + bytes[i + 1] + bytes[i + 2];
                    if (luminance > 128)
                    {
                        ++brightPixels;
                    }
                    else if (luminance < 32)
                    {
                        ++darkPixels;
                    }
                }

                // Procedural starfield: some bright star cores, mostly dark sky.
                CHECK(brightPixels > 50);
                CHECK(darkPixels > (w * h) / 2);

                readback->unmap();

                vkDestroyFence(device->device(), fence, nullptr);
                vkDestroyImageView(device->device(), *colorView, nullptr);
                vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
                vkDestroyCommandPool(device->device(), cmdPool, nullptr);

                colorImage = std::expected<Image, std::string>();
                readback = std::expected<Buffer, std::string>();
            }

            CHECK(allocator->allocationCount() == 0);
        }

        CHECK(context->validationErrorCount() == 0);
    }
}
