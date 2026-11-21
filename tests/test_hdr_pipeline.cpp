// M9.5 HDR/tonemap/bloom acceptance: render an emissive toon object into an
// R16G16B16A16 target, tonemap to UNORM, add a blurred bloom, read back and
// compare against a golden image. Includes a frame-time budget gate.
#include <doctest/doctest.h>

#include "stb_image.h"
#include "stb_image_write.h"

#include "environment/internal/texture_effect.hpp"
#include "environment/toon_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_shader_specialization.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;
namespace math = starforge::math;

namespace
{

constexpr std::uint32_t kW = 128, kH = 128;
constexpr VkDeviceSize kPixelBytes = kW * kH * 4;
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kLdrFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr int kTolerance = 12;

struct Push
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

} // namespace

TEST_CASE("M9.5 HDR: tonemap + bloom golden and budget")
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

    auto createTarget = [&](VkFormat format) {
        auto image = Image::create(*allocator, {
            .width = kW, .height = kH, .format = format,
            .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::Sampled |
                     ImageUsageFlags::TransferSrc});
        REQUIRE(image.has_value());
        auto view = image->createImageView(device->device());
        REQUIRE(view.has_value());
        return std::make_pair(std::move(*image), std::move(*view));
    };
    auto hdr = createTarget(kHdrFormat);
    auto bloomA = createTarget(kHdrFormat);
    auto bloomB = createTarget(kHdrFormat);
    auto finalTarget = createTarget(kLdrFormat);
    auto readback = Buffer::create(*allocator, {
        .size = kPixelBytes, .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

    auto toon = ToonRenderer::create({
        .instance = context->instance(), .device = device->device(),
        .physicalDevice = context->physicalDevice(), .queueFamily = device->graphicsQueueFamily(),
        .framesInFlight = 1, .colorFormat = kHdrFormat, .depthFormat = VK_FORMAT_UNDEFINED,
        .shaderDirectory = STARFORGE_SHADER_DIR,
        .specializationInfo = &hdrOutputSpecialization(true)});
    REQUIRE(toon.has_value());

    const std::string dir = STARFORGE_SHADER_DIR;
    auto tonemap = internal::TextureEffect::create({
        .device = device->device(), .colorFormat = kLdrFormat,
        .shaderDirectory = dir, .shaderBaseName = "hdr_tonemap", .pushConstantSize = 16});
    REQUIRE(tonemap.has_value());
    auto bright = internal::TextureEffect::create({
        .device = device->device(), .colorFormat = kHdrFormat,
        .shaderDirectory = dir, .shaderBaseName = "hdr_bright", .pushConstantSize = 16});
    REQUIRE(bright.has_value());
    auto blur = internal::TextureEffect::create({
        .device = device->device(), .colorFormat = kHdrFormat,
        .shaderDirectory = dir, .shaderBaseName = "hdr_blur", .pushConstantSize = 16});
    REQUIRE(blur.has_value());
    auto add = internal::TextureEffect::create({
        .device = device->device(), .colorFormat = kLdrFormat,
        .shaderDirectory = dir, .shaderBaseName = "texture_blit", .pushConstantSize = 0,
        .additive = true});
    REQUIRE(add.has_value());

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

    auto beginRendering = [&](VkCommandBuffer c, VkImageView view, VkClearColorValue clear) {
        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue.color = clear;
        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = {kW, kH};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering(c, &rendering);
    };
    auto setViewport = [&](VkCommandBuffer c) {
        VkViewport viewport{0.0f, static_cast<float>(kH), static_cast<float>(kW),
                            -static_cast<float>(kH), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {kW, kH}};
        vkCmdSetViewport(c, 0, 1, &viewport);
        vkCmdSetScissor(c, 0, 1, &scissor);
    };
    auto fullPass = [&](auto& effect, VkImageView input, VkImageView output,
                        const Push& push, bool clear = true) {
        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = output;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = {kW, kH};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering(cmd, &rendering);
        setViewport(cmd);
        effect->render(cmd, input, &push, sizeof(push));
        vkCmdEndRendering(cmd);
    };

    auto record = [&]() {
        REQUIRE(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);

        hdr.first.transitionToColorAttachment(cmd);
        beginRendering(cmd, hdr.second.handle(), VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
        setViewport(cmd);
        ToonRenderContext ctx{};
        ctx.viewProj = math::perspectiveVulkan(glm::radians(55.0f), 1.0f, 0.1f, 20.0f) *
                       glm::lookAt(math::Vec3f{0, 0, 4}, math::Vec3f{0, 0, 0}, math::Vec3f{0, 1, 0});
        ctx.modelView = math::Mat4f{1.0f};
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        ToonParams params{};
        params.capabilities = 1u; // emissive
        params.emissiveColor = {1.3f, 0.7f, 0.3f, 1.0f};
        params.baseColor = {0.1f, 0.05f, 0.02f, 1.0f};
        params.outlineWidth = 0.0f;
        toon->render(cmd, ctx, params);
        vkCmdEndRendering(cmd);
        hdr.first.transitionToShaderReadOnly(cmd);

        // Bloom: bright extract -> blur H -> blur V.
        bloomA.first.transitionToColorAttachment(cmd);
        fullPass(bright, hdr.second.handle(), bloomA.second.handle(), Push{1.0f});
        bloomA.first.transitionToShaderReadOnly(cmd);

        bloomB.first.transitionToColorAttachment(cmd);
        fullPass(blur, bloomA.second.handle(), bloomB.second.handle(),
                 Push{1.0f / kW, 1.0f / kH, 0.0f});
        bloomB.first.transitionToShaderReadOnly(cmd);

        bloomA.first.transitionToColorAttachment(cmd);
        fullPass(blur, bloomB.second.handle(), bloomA.second.handle(),
                 Push{1.0f / kW, 1.0f / kH, 1.0f});
        bloomA.first.transitionToShaderReadOnly(cmd);

        // Tonemap HDR -> LDR, then add bloom additively.
        finalTarget.first.transitionToColorAttachment(cmd);
        fullPass(tonemap, hdr.second.handle(), finalTarget.second.handle(), Push{0.35f});
        fullPass(add, bloomA.second.handle(), finalTarget.second.handle(), Push{}, false);

        finalTarget.first.transitionToTransferSrc(cmd);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {kW, kH, 1};
        vkCmdCopyImageToBuffer(cmd, finalTarget.first.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
    };
    auto submit = [&]() {
        REQUIRE(vkResetFences(device->device(), 1, &fence) == VK_SUCCESS);
        VkCommandBufferSubmitInfo submitCmd{};
        submitCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        submitCmd.commandBuffer = cmd;
        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &submitCmd;
        REQUIRE(device->submit(1, &submitInfo, fence) == VK_SUCCESS);
        REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    };

    record();
    submit();

    auto mapped = readback->map();
    REQUIRE(mapped.has_value());
    (void)readback->invalidate(0, kPixelBytes);
    const auto* actual = static_cast<const std::uint8_t*>(*mapped);
    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/hdr_toon_128.png";
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
    std::size_t lit = 0, clipped = 0;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
    {
        if (actual[i] + actual[i + 1] + actual[i + 2] > 30) ++lit;
        if (actual[i] == 255 && actual[i + 1] == 255 && actual[i + 2] == 255) ++clipped;
    }
    readback->unmap();
    CHECK(lit > 100);              // scene is visible after tonemapping
    CHECK(clipped < lit / 2);      // tonemap preserves band detail (not flattened)

    std::vector<double> timings;
    for (int i = 0; i < 12; ++i)
    {
        record();
        const auto start = std::chrono::steady_clock::now();
        submit();
        const auto end = std::chrono::steady_clock::now();
        if (i >= 2) timings.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(timings.begin(), timings.end());
    CHECK(timings[(timings.size() * 95) / 100] < 6.94);
    CHECK(context->validationErrorCount() == 0);

    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
}
