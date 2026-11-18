#include <doctest/doctest.h>

#include "environment/starfield_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "jobs/job_scheduler.hpp"
#include "math/math.hpp"
#include "vulkan/parallel_secondary.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::jobs;
using namespace starforge::math;
using namespace starforge::vulkan;

namespace
{

constexpr std::uint32_t kWidth = 128;
constexpr std::uint32_t kHeight = 128;
constexpr VkDeviceSize kByteSize = kWidth * kHeight * 4;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::size_t kSlots = 4;

struct DrawData
{
    StarfieldRenderer* renderer = nullptr;
    std::array<StarfieldRenderContext, kSlots> contexts{};
    std::array<StarfieldParams, kSlots> params{};
};

void drawStarfieldSecondary(VkCommandBuffer secondary, void* userData, std::uint32_t slot)
{
    auto* data = static_cast<DrawData*>(userData);
    data->renderer->render(secondary, data->contexts[slot], data->params[slot]);
}

VkCommandBuffer makePrimary(VkDevice device, VkCommandPool pool)
{
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(device, &alloc, &cmd) == VK_SUCCESS);
    return cmd;
}

void beginPrimary(VkCommandBuffer cmd)
{
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
}

void submitAndWait(VkDevice device, VkQueue queue, VkCommandBuffer cmd)
{
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    REQUIRE(vkCreateFence(device, &fenceInfo, nullptr, &fence) == VK_SUCCESS);

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = cmd;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdSubmit;
    REQUIRE(vkQueueSubmit2(queue, 1, &submit, fence) == VK_SUCCESS);
    REQUIRE(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    vkDestroyFence(device, fence, nullptr);
}

void copyImageToReadback(VkCommandBuffer cmd, VkImage image, VkBuffer readback)
{
    Image::transitionLayout(cmd, image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {kWidth, kHeight, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);

    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.buffer = readback;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

std::vector<std::uint8_t> readbackBytes(Buffer& readback)
{
    auto mapped = readback.map();
    REQUIRE(mapped.has_value());
    (void)readback.invalidate(0, kByteSize);
    std::vector<std::uint8_t> bytes(static_cast<const std::uint8_t*>(*mapped),
                                    static_cast<const std::uint8_t*>(*mapped) + kByteSize);
    readback.unmap();
    return bytes;
}

} // namespace

TEST_CASE("5.5.5d: recorder rejects invalid configuration")
{
    CHECK_FALSE(ParallelSecondaryRecorder::create({}).has_value());
    CHECK_FALSE(ParallelSecondaryRecorder::create({.device = reinterpret_cast<VkDevice>(1)}).has_value());
}

TEST_CASE("5.5.5d: parallel secondary recording matches sequential rendering (T3)")
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
            auto parallelImage = Image::create(*allocator, {
                .width = kWidth, .height = kHeight,
                .format = kColorFormat, .usage = ImageUsageFlags::ColorAttachment,
            });
            REQUIRE(parallelImage.has_value());
            auto parallelView = parallelImage->createImageView(device->device());
            REQUIRE(parallelView.has_value());

            auto referenceImage = Image::create(*allocator, {
                .width = kWidth, .height = kHeight,
                .format = kColorFormat, .usage = ImageUsageFlags::ColorAttachment,
            });
            REQUIRE(referenceImage.has_value());
            auto referenceView = referenceImage->createImageView(device->device());
            REQUIRE(referenceView.has_value());

            auto parallelReadback = Buffer::create(*allocator, {
                .size = kByteSize, .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(parallelReadback.has_value());
            auto referenceReadback = Buffer::create(*allocator, {
                .size = kByteSize, .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(referenceReadback.has_value());

            auto starRenderer = StarfieldRenderer::create({
                .device = device->device(),
                .physicalDevice = context->physicalDevice(),
                .queueFamily = device->graphicsQueueFamily(),
                .colorFormat = kColorFormat,
                .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
            });
            REQUIRE(starRenderer.has_value());

            JobScheduler scheduler{SchedulerConfig{.threadCount = static_cast<std::uint32_t>(kSlots)}};

            auto recorder = ParallelSecondaryRecorder::create({
                .device = device->device(),
                .queueFamily = device->graphicsQueueFamily(),
                .colorFormat = kColorFormat,
                .depthFormat = VK_FORMAT_UNDEFINED,
                .width = kWidth,
                .height = kHeight,
                .maxThreads = static_cast<std::uint32_t>(kSlots),
                .framesInFlight = 1,
            });
            REQUIRE(recorder.has_value());

            DrawData draw;
            draw.renderer = &*starRenderer;
            const auto viewProj = perspectiveVulkan(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) *
                                  glm::lookAt(Vec3f{0.0f, 0.0f, 5.0f}, Vec3f{0.0f, 0.0f, 0.0f},
                                              Vec3f{0.0f, 1.0f, 0.0f});
            for (std::size_t i = 0; i < kSlots; ++i)
            {
                auto& ctx = draw.contexts[i];
                ctx.resolution = {static_cast<float>(kWidth), static_cast<float>(kHeight)};
                ctx.time = 1.0f + static_cast<float>(i);
                ctx.viewProj = viewProj;
                ctx.invViewProj = glm::inverse(viewProj);
                auto& params = draw.params[i];
                params.time = ctx.time;
                params.intensity = 1.0f;
                params.worldPosition = {static_cast<float>(i) * 0.5f, 0.0f, 0.0f};
                params.radius = 2.0f;
                params.color = {1.0f, 0.9f, 0.6f, 1.0f};
            }

            std::array<SecondaryTask, kSlots> tasks{};
            for (auto& task : tasks) task = SecondaryTask{&drawStarfieldSecondary, &draw};
            auto secondaries = recorder->record(0, tasks, scheduler);
            REQUIRE(secondaries.has_value());
            CHECK(secondaries->size() == kSlots);
            CHECK(recorder->lastRecordingThreads().size() == kSlots);

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            VkCommandPool pool = VK_NULL_HANDLE;
            REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &pool) == VK_SUCCESS);

            VkRenderingAttachmentInfo attachment{};
            attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView = *parallelView;
            attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
            rendering.renderArea.extent = {kWidth, kHeight};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &attachment;

            // Parallel path: execute all secondaries produced by worker jobs.
            {
                VkCommandBuffer cmd = makePrimary(device->device(), pool);
                beginPrimary(cmd);
                Image::transitionToColorAttachment(cmd, parallelImage->handle());
                vkCmdBeginRendering(cmd, &rendering);
                vkCmdExecuteCommands(cmd, static_cast<std::uint32_t>(secondaries->size()), secondaries->data());
                vkCmdEndRendering(cmd);
                copyImageToReadback(cmd, parallelImage->handle(), parallelReadback->handle());
                REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);
                submitAndWait(device->device(), device->graphicsQueue(), cmd);
                vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
            }

            // Reference path: the same four draws recorded sequentially.
            {
                attachment.imageView = *referenceView;
                VkRenderingInfo sequential = rendering;
                sequential.flags = 0;
                sequential.pColorAttachments = &attachment;

                VkCommandBuffer cmd = makePrimary(device->device(), pool);
                beginPrimary(cmd);
                Image::transitionToColorAttachment(cmd, referenceImage->handle());
                vkCmdBeginRendering(cmd, &sequential);

                VkViewport viewport{};
                viewport.y = static_cast<float>(kHeight);
                viewport.width = static_cast<float>(kWidth);
                viewport.height = -static_cast<float>(kHeight);
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor{};
                scissor.extent = {kWidth, kHeight};
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                for (std::size_t i = 0; i < kSlots; ++i)
                {
                    starRenderer->render(cmd, draw.contexts[i], draw.params[i]);
                }

                vkCmdEndRendering(cmd);
                copyImageToReadback(cmd, referenceImage->handle(), referenceReadback->handle());
                REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);
                submitAndWait(device->device(), device->graphicsQueue(), cmd);
                vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
            }

            const auto parallelBytes = readbackBytes(*parallelReadback);
            const auto referenceBytes = readbackBytes(*referenceReadback);
            CHECK(parallelBytes == referenceBytes);

            std::size_t brightPixels = 0;
            for (std::size_t i = 0; i < kByteSize; i += 4)
            {
                if (static_cast<std::uint32_t>(parallelBytes[i]) + parallelBytes[i + 1] +
                        parallelBytes[i + 2] > 128)
                {
                    ++brightPixels;
                }
            }
            CHECK(brightPixels > 50);

            // Resetting the per-frame pools allows a second recording pass.
            recorder->resetFrame(0);
            auto secondaries2 = recorder->record(0, tasks, scheduler);
            REQUIRE(secondaries2.has_value());
            CHECK(secondaries2->size() == kSlots);

            vkDestroyCommandPool(device->device(), pool, nullptr);
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
