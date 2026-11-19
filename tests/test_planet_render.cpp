// M15.0.4 planet ray-sphere billboard + atmosphere limb golden (T3).
// Renders an opaque planet disc with projective depth and a Fresnel atmosphere
// halo into an offscreen target, reads it back and compares to a golden image.
#include <doctest/doctest.h>

#include "stb_image.h"
#include "stb_image_write.h"

#include "environment/planet_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <string>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif
#ifndef STARFORGE_GOLDEN_DIR
#define STARFORGE_GOLDEN_DIR "tests/goldens"
#endif

using namespace starforge;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

namespace
{
constexpr std::uint32_t kW = 128, kH = 128;
constexpr VkDeviceSize kPixelBytes = kW * kH * 4;
constexpr int kTolerance = 12;
} // namespace

TEST_CASE("M15.0.4: planet ray-sphere + atmosphere limb golden (T3)")
{
    ContextCreateInfo info;
    info.enableValidation = true;
    info.requiredExtensions.clear();
    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());
    auto device = VulkanDevice::create(*context);
    REQUIRE(device.has_value());
    auto allocator = Allocator::create({
        .instance = context->instance(), .physicalDevice = context->physicalDevice(),
        .device = device->device()});
    REQUIRE(allocator.has_value());

    VkFormat depthFormat = findDepthFormat(context->physicalDevice());
    REQUIRE(depthFormat != VK_FORMAT_UNDEFINED);

    auto color = Image::create(*allocator, {
        .width = kW, .height = kH, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc});
    REQUIRE(color.has_value());
    auto colorView = color->createImageView(device->device());
    REQUIRE(colorView.has_value());

    auto depth = Image::create(*allocator, {
        .width = kW, .height = kH, .format = depthFormat,
        .usage = ImageUsageFlags::DepthAttachment});
    REQUIRE(depth.has_value());
    auto depthView = depth->createImageView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
    REQUIRE(depthView.has_value());

    auto readback = Buffer::create(*allocator, {
        .size = kPixelBytes, .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

    auto planet = environment::PlanetRenderer::create({
        .device = device->device(), .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = depthFormat, .shaderDirectory = STARFORGE_SHADER_DIR});
    REQUIRE(planet.has_value());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = pool;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(device->device(), &cmdAlloc, &cmd) == VK_SUCCESS);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);

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
    depthAttachment.clearValue.depthStencil = {1.0f, 0}; // standard depth clear
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

    const math::Vec3f eye{0.0f, 0.0f, 3000.0f};
    const auto view = glm::lookAt(eye, math::Vec3f{0.0f, 0.0f, 0.0f}, math::Vec3f{0.0f, 1.0f, 0.0f});
    const auto proj = math::perspectiveVulkan(glm::radians(60.0f), 1.0f, 10.0f, 20000.0f);
    environment::PlanetRenderContext ctx;
    ctx.viewProj = proj * view;
    ctx.invViewProj = glm::inverse(ctx.viewProj);
    ctx.cameraPosition = eye;
    ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};

    environment::PlanetParams params;
    params.center = math::Vec3f{0.0f, 0.0f, 0.0f};
    params.radius = 1000.0f;
    params.baseColor = {0.18f, 0.5f, 1.0f, 1.0f};
    params.shadowColor = {0.02f, 0.1f, 0.3f, 1.0f};
    params.rimColor = {0.9f, 0.7f, 0.45f, 1.0f}; // warm sunset limb
    params.sunDirection = glm::normalize(math::Vec3f{0.85f, 0.25f, 0.3f}); // side-on -> terminator
    params.fresnelPower = 3.5f;
    params.rimStrength = 1.3f;
    params.atmosphereRatio = 1.2f;
    params.atmosphereColor = {0.35f, 0.6f, 1.0f, 1.0f};
    params.atmosphereIntensity = 1.4f;
    planet->render(cmd, ctx, params);
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
    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/planet_ray_sphere_128.png";
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

    // The planet disc is lit and blue-ish; some atmosphere halo pixels appear
    // just outside it. This is a visual-identity check, not just a pixel count.
    std::size_t lit = 0, blue = 0;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
    {
        if (actual[i] + actual[i + 1] + actual[i + 2] > 40) ++lit;
        if (actual[i + 2] > actual[i] + 20 && actual[i + 2] > 60) ++blue;
    }
    readback->unmap();
    CHECK(lit > 800);  // planet + halo cover a healthy part of the frame
    CHECK(blue > 400); // blue planet / atmosphere identity
    CHECK(context->validationErrorCount() == 0);

    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
}
