#include <doctest/doctest.h>

#include "environment/asteroid_field.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::math;
using namespace starforge::vulkan;

namespace
{

std::expected<Buffer, std::string> uploadBuffer(Allocator& allocator, const void* data, std::size_t byteSize)
{
    auto buffer = Buffer::create(allocator, {
        .size = static_cast<VkDeviceSize>(byteSize),
        .usage = BufferUsage::Storage,
        .memory = MemoryPlacement::CpuToGpu,
    });
    if (!buffer)
    {
        return std::unexpected(std::move(buffer.error()));
    }
    auto mapped = buffer->map();
    if (!mapped)
    {
        return std::unexpected(mapped.error());
    }
    std::memcpy(*mapped, data, byteSize);
    (void)buffer->flush(0, static_cast<VkDeviceSize>(byteSize));
    buffer->unmap();
    return buffer;
}

} // namespace

TEST_CASE("Asteroid field: instanced rocks render with depth (8.6)")
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

            auto mesh = generateAsteroidMesh(1.0f, 12, 8);
            REQUIRE(!mesh.vertices.empty());
            REQUIRE(!mesh.indices.empty());

            auto vertexBuffer = uploadBuffer(*allocator, mesh.vertices.data(), mesh.vertices.size() * sizeof(float));
            REQUIRE(vertexBuffer.has_value());
            auto indexBuffer = uploadBuffer(*allocator, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
            REQUIRE(indexBuffer.has_value());

            // 200 instances: one at origin, rest in a shell around it.
            // Quaternion from axis-angle (deterministic per instance).
            struct InstanceData
            {
                float posScale[4];
                float quat[4];
            };
            static_assert(sizeof(InstanceData) == 32, "InstanceData must be 32 bytes");
            const auto makeQuat = [](std::uint32_t seed) {
                const float ax = static_cast<float>(seed % 7) / 7.0f - 0.5f;
                const float ay = static_cast<float>((seed / 3) % 7) / 7.0f - 0.5f;
                const float az = static_cast<float>((seed / 11) % 7) / 7.0f - 0.5f;
                float len = std::sqrt(ax * ax + ay * ay + az * az);
                if (len < 1e-6f) len = 1.0f;
                const float half = static_cast<float>(seed % 10) * 0.3f;
                const float s = std::sin(half) / len;
                return std::array<float, 4>{ax * s, ay * s, az * s, std::cos(half)};
            };
            std::vector<InstanceData> instances;
            instances.reserve(200);
            instances.push_back({{0.0f, 0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f, 1.0f}});
            for (std::uint32_t i = 1; i < 200; ++i)
            {
                const float ang = static_cast<float>(i) * 2.399963f; // Golden-angle spiral
                const float rad = 10.0f + static_cast<float>((i * 37) % 25);
                const auto q = makeQuat(i);
                instances.push_back({
                    {std::cos(ang) * rad, std::sin(ang * 1.3f) * rad * 0.5f, std::sin(ang) * rad, 2.5f},
                    {q[0], q[1], q[2], q[3]},
                });
            }
            auto instanceBuffer = uploadBuffer(*allocator, instances.data(), instances.size() * sizeof(InstanceData));
            REQUIRE(instanceBuffer.has_value());

            auto colorImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .usage = ImageUsageFlags::ColorAttachment,
            });
            REQUIRE(colorImage.has_value());
            auto colorView = colorImage->createView(device->device());
            REQUIRE(colorView.has_value());

            VkFormat depthFormat = findDepthFormat(context->physicalDevice());
            REQUIRE(depthFormat != VK_FORMAT_UNDEFINED);
            auto depthImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = depthFormat,
                .usage = ImageUsageFlags::DepthAttachment,
            });
            REQUIRE(depthImage.has_value());
            auto depthView = depthImage->createView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
            REQUIRE(depthView.has_value());

            auto readback = Buffer::create(*allocator, {
                .size = w * h * 4,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            auto renderer = AsteroidFieldRenderer::create({
                .device = device->device(),
                .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                .depthFormat = depthFormat,
                .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
            });
            REQUIRE(renderer.has_value());

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
            Image::transitionLayout(cmd, depthImage->handle(),
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = *colorView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            // Pure black clears unambiguously (no quantized comparisons).
            colorAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderingAttachmentInfo depthAttachment{};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = *depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil = VkClearDepthStencilValue{1.0f, 0};

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = {0, 0};
            renderingInfo.renderArea.extent = {w, h};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;

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

            AsteroidFieldRenderContext fieldCtx;
            fieldCtx.viewProj = perspectiveVulkan(glm::radians(60.0f), 1.0f, 0.5f, 500.0f) *
                                glm::lookAt(Vec3f{0.0f, 0.0f, 35.0f}, Vec3f{0.0f, 0.0f, 0.0f},
                                            Vec3f{0.0f, 1.0f, 0.0f});
            fieldCtx.resolution = {static_cast<float>(w), static_cast<float>(h)};

            AsteroidFieldParams params;
            params.vertexBufferAddress = vertexBuffer->deviceAddress(device->device());
            params.instanceBufferAddress = instanceBuffer->deviceAddress(device->device());
            params.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
            params.instanceCount = static_cast<std::uint32_t>(instances.size());
            params.time = 1.0f;

            vkCmdBindIndexBuffer(cmd, indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
            renderer->render(cmd, fieldCtx, params);

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
            (void)readback->invalidate(0, w * h * 4);
            const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
            std::uint32_t litPixels = 0;
            for (std::size_t i = 0; i < w * h * 4; i += 4)
            {
                if (bytes[i] != 0 || bytes[i + 1] != 0 || bytes[i + 2] != 0)
                {
                    ++litPixels;
                }
            }
            // Rocks cover a visible fraction of the frame.
            CHECK(litPixels > (w * h) / 10);
            readback->unmap();

            vkDestroyFence(device->device(), fence, nullptr);
            vkDestroyImageView(device->device(), *colorView, nullptr);
            vkDestroyImageView(device->device(), *depthView, nullptr);
            vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
            vkDestroyCommandPool(device->device(), cmdPool, nullptr);

            colorImage = std::expected<Image, std::string>();
            depthImage = std::expected<Image, std::string>();
            readback = std::expected<Buffer, std::string>();
            vertexBuffer = std::expected<Buffer, std::string>();
            indexBuffer = std::expected<Buffer, std::string>();
            instanceBuffer = std::expected<Buffer, std::string>();
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
