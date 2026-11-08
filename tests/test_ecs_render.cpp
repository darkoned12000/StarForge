// ECS render integration + M10 exit proof (10.3/10.4): 10,000 ECS
// asteroid entities are stepped, collected into the instanced asteroid
// stream, uploaded, and rendered offscreen within the ADR-004 frame
// interval.
#include <doctest/doctest.h>

#include "ecs/instance_stream.hpp"
#include "ecs/world.hpp"
#include "environment/asteroid_field.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::ecs;
using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;
namespace math = starforge::math;
namespace spatial = starforge::spatial;

namespace
{

constexpr std::uint32_t kEntityCount = 10000;
constexpr std::uint32_t kW = 128;
constexpr std::uint32_t kH = 128;

[[nodiscard]] spatial::WorldPosition spawnPosition(std::uint32_t i)
{
    // Golden-angle spiral shell around (0, 0, -25), deterministic.
    const float ang = static_cast<float>(i) * 2.399963f;
    const float rad = 6.0f + static_cast<float>((i * 37) % 100) * 0.14f;
    const float x = std::cos(ang) * rad;
    const float y = std::sin(ang * 1.3f) * rad * 0.6f;
    const float z = -25.0f + std::sin(ang) * rad * 0.5f;
    auto position = spatial::WorldPosition::fromMeters(x, y, z);
    REQUIRE(position.has_value());
    return *position;
}

} // namespace

TEST_CASE("ECS render: 10k entities stepped, collected, and rendered within budget (10.4)")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    auto device = VulkanDevice::create(*context);
    REQUIRE(device.has_value());

    auto allocator = Allocator::create({
        .instance = context->instance(),
        .physicalDevice = context->physicalDevice(),
        .device = device->device(),
    });
    REQUIRE(allocator.has_value());

    // ECS world: 10k asteroids with small deterministic velocities.
    Ecs world;
    for (std::uint32_t i = 0; i < kEntityCount; ++i)
    {
        const auto entity = world.createAsteroid(spawnPosition(i), {0.5, 0.25, 0.0});
        auto& transform = entity.get_mut<Transform>();
        transform.scale = 1.0f + static_cast<float>(i % 5) * 0.5f;
        entity.modified<Transform>();
    }
    REQUIRE(world.entityCount() == kEntityCount);

    const auto camera = spatial::WorldPosition::fromMeters(0, 0, 0);
    REQUIRE(camera.has_value());

    const auto mesh = generateAsteroidMesh(1.0f, 12, 8);
    REQUIRE(!mesh.vertices.empty());

    auto uploadBufferFor = [&allocator](const void* data, std::size_t byteSize) {
        auto buffer = Buffer::create(*allocator, {
            .size = static_cast<VkDeviceSize>(byteSize),
            .usage = BufferUsage::Storage,
            .memory = MemoryPlacement::CpuToGpu,
        });
        REQUIRE(buffer.has_value());
        auto mapped = buffer->map();
        REQUIRE(mapped.has_value());
        std::memcpy(*mapped, data, byteSize);
        (void)buffer->flush(0, static_cast<VkDeviceSize>(byteSize));
        buffer->unmap();
        return std::move(*buffer);
    };

    auto vertexBuffer = uploadBufferFor(mesh.vertices.data(), mesh.vertices.size() * sizeof(float));
    auto indexBuffer = uploadBufferFor(mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));

    // Instance buffer, created once at full capacity and rewritten per frame.
    auto instanceBuffer = Buffer::create(*allocator, {
        .size = static_cast<VkDeviceSize>(kEntityCount * sizeof(AsteroidInstance)),
        .usage = BufferUsage::Storage,
        .memory = MemoryPlacement::CpuToGpu,
    });
    REQUIRE(instanceBuffer.has_value());

    auto colorImage = Image::create(*allocator, {
        .width = kW,
        .height = kH,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsageFlags::ColorAttachment,
    });
    REQUIRE(colorImage.has_value());
    auto colorView = colorImage->createView(device->device());
    REQUIRE(colorView.has_value());

    const VkFormat depthFormat = findDepthFormat(context->physicalDevice());
    REQUIRE(depthFormat != VK_FORMAT_UNDEFINED);
    auto depthImage = Image::create(*allocator, {
        .width = kW,
        .height = kH,
        .format = depthFormat,
        .usage = ImageUsageFlags::DepthAttachment,
    });
    REQUIRE(depthImage.has_value());
    auto depthView = depthImage->createView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
    REQUIRE(depthView.has_value());

    auto readback = Buffer::create(*allocator, {
        .size = kW * kH * 4,
        .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu,
    });
    REQUIRE(readback.has_value());

    auto renderer = AsteroidFieldRenderer::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = depthFormat,
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    REQUIRE(renderer.has_value());

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

    std::vector<AsteroidInstance> instances;
    instances.reserve(kEntityCount);

    constexpr int kFrames = 30;
    std::vector<double> timings;
    timings.reserve(kFrames);

    for (int frame = 0; frame < kFrames; ++frame)
    {
        const auto frameStart = std::chrono::steady_clock::now();

        // ECS step + collect + upload.
        world.step(0.016f);
        const std::uint32_t count = collectAsteroidInstances(world, *camera, instances);
        REQUIRE(count == kEntityCount);
        auto mapped = instanceBuffer->map();
        REQUIRE(mapped.has_value());
        std::memcpy(*mapped, instances.data(), instances.size() * sizeof(AsteroidInstance));
        (void)instanceBuffer->flush(0, static_cast<VkDeviceSize>(instances.size() * sizeof(AsteroidInstance)));
        instanceBuffer->unmap();

        // Record and submit.
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        Image::transitionToColorAttachment(cmd, colorImage->handle());
        Image::transitionLayout(cmd, depthImage->handle(),
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_DEPTH_BIT,
                                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = *colorView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
        renderingInfo.renderArea.extent = {kW, kH};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

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

        AsteroidFieldRenderContext renderContext;
        const auto view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        renderContext.viewProj = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) * view;
        renderContext.cameraPosition = {0.0f, 0.0f, 0.0f};
        renderContext.resolution = {static_cast<float>(kW), static_cast<float>(kH)};

        AsteroidFieldParams params;
        params.vertexBufferAddress = vertexBuffer.deviceAddress(device->device());
        params.instanceBufferAddress = instanceBuffer->deviceAddress(device->device());
        params.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        params.instanceCount = count;
        params.time = static_cast<float>(frame) * 0.016f;
        vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);
        renderer->render(cmd, renderContext, params);

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

        vkEndCommandBuffer(cmd);

        VkCommandBufferSubmitInfo cmdSubmit{};
        cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdSubmit.commandBuffer = cmd;
        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdSubmit;
        vkQueueSubmit2(device->graphicsQueue(), 1, &submitInfo, fence);
        vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device->device(), 1, &fence);

        const auto frameEnd = std::chrono::steady_clock::now();
        if (frame >= 3)
        {
            timings.push_back(std::chrono::duration<double, std::milli>(frameEnd - frameStart).count());
        }
    }

    // Entities actually rendered: a solid fraction of pixels is non-black.
    auto mapped = readback->map();
    REQUIRE(mapped.has_value());
    (void)readback->invalidate(0, kW * kH * 4);
    const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
    std::uint32_t litPixels = 0;
    for (std::uint32_t i = 0; i < kW * kH * 4; i += 4)
    {
        if (bytes[i] != 0 || bytes[i + 1] != 0 || bytes[i + 2] != 0)
        {
            ++litPixels;
        }
    }
    readback->unmap();
    CHECK(litPixels > (kW * kH) / 4);

    std::sort(timings.begin(), timings.end());
    const double p95 = timings[(timings.size() * 95) / 100];
    CHECK(p95 < 6.94);

    vkDestroyFence(device->device(), fence, nullptr);
    vkDestroyImageView(device->device(), *colorView, nullptr);
    vkDestroyImageView(device->device(), *depthView, nullptr);
    vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
    vkDestroyCommandPool(device->device(), cmdPool, nullptr);

    CHECK(context->validationErrorCount() == 0);
}
