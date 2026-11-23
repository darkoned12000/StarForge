// M17.5b/c/d T3 proof: one instanced additive draw renders all combat
// effect shapes (bolts, sparks, shockwaves, fireballs and engine plumes)
// into an offscreen target with zero validation errors.
#include <doctest/doctest.h>

#include "environment/combat_fx_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"
#include "vulkan/vulkan_shader_specialization.hpp"

#include "stb_image.h"
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif
#ifndef STARFORGE_GOLDEN_DIR
#define STARFORGE_GOLDEN_DIR "tests/goldens"
#endif

namespace
{
using namespace starforge;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

constexpr std::uint32_t kW = 128;
constexpr std::uint32_t kH = 128;
constexpr std::size_t kPixelBytes = static_cast<std::size_t>(kW) * kH * 4;
constexpr std::uint32_t kTolerance = 12;

struct FxInstance
{
    float posRadius[4]{};
    float direction[4]{};
    float color[4]{};
    float misc[4]{};
};
static_assert(sizeof(FxInstance) == 64);

[[nodiscard]] std::expected<std::unique_ptr<Buffer>, std::string>
upload(Allocator& allocator, const void* data, VkDeviceSize bytes, BufferUsage usage)
{
    auto buffer = Buffer::create(allocator, {.size = bytes, .usage = usage,
                                             .memory = MemoryPlacement::CpuToGpu,
                                             .name = "test.CombatFx.Instances"});
    if (!buffer) return std::unexpected(buffer.error());
    auto mapped = buffer->map();
    if (!mapped) return std::unexpected(mapped.error());
    std::memcpy(*mapped, data, static_cast<std::size_t>(bytes));
    (void)buffer->flush();
    buffer->unmap();
    return std::make_unique<Buffer>(std::move(*buffer));
}

[[nodiscard]] FxInstance makeInstance(float x, float y, float z, float radius,
                                      float kind, const std::array<float, 3>& rgb,
                                      float intensity, float age, float seed, float fade)
{
    FxInstance out{};
    out.posRadius[0] = x; out.posRadius[1] = y;
    out.posRadius[2] = z; out.posRadius[3] = radius;
    out.direction[3] = kind;
    out.color[0] = rgb[0]; out.color[1] = rgb[1];
    out.color[2] = rgb[2]; out.color[3] = intensity;
    out.misc[0] = age; out.misc[1] = seed;
    out.misc[2] = 1.0f; out.misc[3] = fade;
    return out;
}
} // namespace

TEST_CASE("M17.5: instanced combat effects render every shape (T3 golden)")
{
    ContextCreateInfo info;
    info.enableValidation = true;
    info.requiredExtensions.clear();
    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());
    auto device = VulkanDevice::create(*context);
    REQUIRE(device.has_value());
    auto allocator = Allocator::create({.instance = context->instance(),
                                        .physicalDevice = context->physicalDevice(),
                                        .device = device->device()});
    REQUIRE(allocator.has_value());
    const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    const VkFormat depthFormat = findDepthFormat(context->physicalDevice());
    REQUIRE(depthFormat != VK_FORMAT_UNDEFINED);

    std::vector<FxInstance> instances;
    // Two hot bolts with white cores.
    for (int i = 0; i < 2; ++i)
    {
        auto bolt = makeInstance(-8.0f + i * 16.0f, -6.0f, 0.0f, 9.0f, 0.0f,
                                 {0.3f, 0.8f, 1.0f}, 3.0f, 0.1f, 0.0f, 1.0f);
        bolt.direction[0] = 0.0f; bolt.direction[1] = 1.0f; bolt.direction[2] = 0.0f;
        instances.push_back(bolt);
    }
    instances.push_back(makeInstance(-22.0f, 6.0f, 0.0f, 7.0f, 1.0f,
                                     {1.0f, 0.6f, 0.2f}, 2.0f, 0.1f, 3.0f, 1.0f)); // sparks
    instances.push_back(makeInstance(0.0f, 0.0f, 0.0f, 20.0f, 2.0f,
                                     {0.25f, 0.85f, 1.0f}, 2.0f, 0.2f, 7.0f, 1.0f)); // shield ring
    instances.push_back(makeInstance(20.0f, 12.0f, 0.0f, 12.0f, 3.0f,
                                     {1.0f, 0.45f, 0.15f}, 3.0f, 0.3f, 11.0f, 1.0f)); // fireball
    auto plume = makeInstance(0.0f, -30.0f, 0.0f, 7.0f, 4.0f,
                              {0.2f, 0.6f, 1.0f}, 2.0f, 0.5f, 17.0f, 1.0f);
    plume.direction[1] = -1.0f; // exhaust points down-screen
    plume.misc[2] = 4.5f;       // ribbon half-width (exaggerated for the golden)
    instances.push_back(plume);

    auto instanceBuffer = upload(*allocator, instances.data(),
                                 instances.size() * sizeof(FxInstance), BufferUsage::Storage);
    REQUIRE(instanceBuffer.has_value());

    auto color = Image::create(*allocator, {.width = kW, .height = kH, .format = colorFormat,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc});
    REQUIRE(color.has_value());
    auto colorView = color->createImageView(device->device());
    REQUIRE(colorView.has_value());
    auto depth = Image::create(*allocator, {.width = kW, .height = kH, .format = depthFormat,
        .usage = ImageUsageFlags::DepthAttachment});
    REQUIRE(depth.has_value());
    auto depthView = depth->createImageView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
    REQUIRE(depthView.has_value());
    auto readback = Buffer::create(*allocator, {.size = kPixelBytes,
        .usage = BufferUsage::TransferDst, .memory = MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

    auto renderer = environment::CombatFxRenderer::create({
        .device = device->device(), .colorFormat = colorFormat, .depthFormat = depthFormat,
        .shaderDirectory = STARFORGE_SHADER_DIR,
        .specializationInfo = &vulkan::hdrOutputSpecialization(false)});
    REQUIRE(renderer.has_value());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(device->device(), &alloc, &cmd) == VK_SUCCESS);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);
    color->transitionToColorAttachment(cmd);
    Image::transitionLayout(cmd, depth->handle(), VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorView->handle();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthView->handle();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {kW, kH};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colorAttachment;
    rendering.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(cmd, &rendering);
    VkViewport viewport{0.0f, static_cast<float>(kH), static_cast<float>(kW),
                        -static_cast<float>(kH), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {kW, kH}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    environment::CombatFxRenderContext rctx;
    const auto view = glm::lookAt(math::Vec3f{0.0f, 0.0f, 60.0f},
                                       math::Vec3f{0.0f, 0.0f, 0.0f},
                                       math::Vec3f{0.0f, 1.0f, 0.0f});
    const auto proj = math::perspectiveVulkan(1.0471976f, 1.0f, 0.1f, 500.0f);
    rctx.viewProj = proj * view;
    rctx.cameraRight = {1.0f, 0.0f, 0.0f, 0.0f};
    rctx.cameraUp = {0.0f, 1.0f, 0.0f, 0.0f};
    rctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
    renderer->render(cmd, rctx, environment::CombatFxDrawParams{
        .instanceAddress = (*instanceBuffer)->deviceAddress(device->device()),
        .instanceCount = static_cast<std::uint32_t>(instances.size())});
    vkCmdEndRendering(cmd);

    color->transitionToTransferSrc(cmd);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {kW, kH, 1};
    vkCmdCopyImageToBuffer(cmd, color->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->handle(), 1, &copy);
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.buffer = readback->handle();
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
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

    auto mapped = readback->map();
    REQUIRE(mapped.has_value());
    (void)readback->invalidate(0, kPixelBytes);
    const auto* actual = static_cast<const std::uint8_t*>(*mapped);
    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/combat_fx_128.png";
    if (std::getenv("STARFORGE_UPDATE_GOLDENS") != nullptr)
    {
        REQUIRE(stbi_write_png(goldenPath.c_str(), kW, kH, 4, actual, kW * 4) == 1);
    }
    else
    {
        int gw = 0, gh = 0, gc = 0;
        unsigned char* golden = stbi_load(goldenPath.c_str(), &gw, &gh, &gc, 4);
        REQUIRE_MESSAGE(golden != nullptr, "Golden missing: " << goldenPath);
        std::uint32_t failing = 0;
        for (std::size_t i = 0; i < kPixelBytes; ++i)
            if (std::abs(static_cast<int>(actual[i]) - static_cast<int>(golden[i])) > kTolerance) ++failing;
        stbi_image_free(golden);
        CHECK(static_cast<double>(failing) / kPixelBytes < 0.01);
    }

    std::size_t lit = 0;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
        if (actual[i] + actual[i + 1] + actual[i + 2] > 40) ++lit;
    CHECK(lit > 300);
    CHECK(context->validationErrorCount() == 0);
    readback->unmap();

    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
}

TEST_CASE("M17.5b: 256 bolts clamp to capacity without validation errors")
{
    ContextCreateInfo info;
    info.enableValidation = true;
    info.requiredExtensions.clear();
    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());
    auto device = VulkanDevice::create(*context);
    REQUIRE(device.has_value());
    auto allocator = Allocator::create({.instance = context->instance(),
                                        .physicalDevice = context->physicalDevice(),
                                        .device = device->device()});
    REQUIRE(allocator.has_value());
    const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    const VkFormat depthFormat = findDepthFormat(context->physicalDevice());

    std::vector<FxInstance> bolts;
    bolts.reserve(256);
    for (int i = 0; i < 256; ++i)
    {
        auto bolt = makeInstance(static_cast<float>((i % 16) * 4 - 30),
                                 static_cast<float>((i / 16) * 4 - 30), 0.0f,
                                 3.0f, 0.0f, {0.35f, 0.85f, 1.0f}, 2.0f, 0.1f, 0.0f, 1.0f);
        bolt.direction[1] = 1.0f;
        bolts.push_back(bolt);
    }
    auto instanceBuffer = upload(*allocator, bolts.data(), bolts.size() * sizeof(FxInstance),
                                 BufferUsage::Storage);
    REQUIRE(instanceBuffer.has_value());
    auto renderer = environment::CombatFxRenderer::create({
        .device = device->device(), .colorFormat = colorFormat, .depthFormat = depthFormat,
        .shaderDirectory = STARFORGE_SHADER_DIR,
        .specializationInfo = &vulkan::hdrOutputSpecialization(false)});
    REQUIRE(renderer.has_value());

    auto color = Image::create(*allocator, {.width = kW, .height = kH, .format = colorFormat,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc});
    REQUIRE(color.has_value());
    auto colorView = color->createImageView(device->device());
    REQUIRE(colorView.has_value());
    auto depth = Image::create(*allocator, {.width = kW, .height = kH, .format = depthFormat,
        .usage = ImageUsageFlags::DepthAttachment});
    REQUIRE(depth.has_value());
    auto depthView = depth->createImageView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
    REQUIRE(depthView.has_value());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(device->device(), &alloc, &cmd) == VK_SUCCESS);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

    for (int frame = 0; frame < 3; ++frame)
    {
        REQUIRE(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
        VkCommandBufferBeginInfo frameBegin{};
        frameBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        frameBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        REQUIRE(vkBeginCommandBuffer(cmd, &frameBegin) == VK_SUCCESS);
        color->transitionToColorAttachment(cmd);
        Image::transitionLayout(cmd, depth->handle(), VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo ca{};
        ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ca.imageView = colorView->handle();
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ca.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderingAttachmentInfo da{};
        da.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        da.imageView = depthView->handle();
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        da.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = {kW, kH};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &ca;
        rendering.pDepthAttachment = &da;
        vkCmdBeginRendering(cmd, &rendering);
        VkViewport viewport{0.0f, static_cast<float>(kH), static_cast<float>(kW),
                            -static_cast<float>(kH), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, {kW, kH}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        environment::CombatFxRenderContext rctx;
        const auto view = glm::lookAt(math::Vec3f{0.0f, 0.0f, 60.0f},
                                           math::Vec3f{0.0f, 0.0f, 0.0f},
                                           math::Vec3f{0.0f, 1.0f, 0.0f});
        const auto proj = math::perspectiveVulkan(1.0471976f, 1.0f, 0.1f, 500.0f);
        rctx.viewProj = proj * view;
        rctx.cameraRight = {1.0f, 0.0f, 0.0f, 0.0f};
        rctx.cameraUp = {0.0f, 1.0f, 0.0f, 0.0f};
        rctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        renderer->render(cmd, rctx, environment::CombatFxDrawParams{
            .instanceAddress = (*instanceBuffer)->deviceAddress(device->device()),
            .instanceCount = 256});
        vkCmdEndRendering(cmd);
        REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);

        VkCommandBufferSubmitInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        sci.commandBuffer = cmd;
        VkSubmitInfo2 sub{};
        sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        sub.commandBufferInfoCount = 1;
        sub.pCommandBufferInfos = &sci;
        REQUIRE(device->submit(1, &sub, fence) == VK_SUCCESS);
        REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
        REQUIRE(vkResetFences(device->device(), 1, &fence) == VK_SUCCESS);
    }
    CHECK(context->validationErrorCount() == 0);
    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
}
