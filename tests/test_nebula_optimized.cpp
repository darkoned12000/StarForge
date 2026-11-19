// M8 residual / M15.0.6: seeded 3D-noise nebula with a half-resolution
// offscreen target. Verifies deterministic volume, golden output, the >=75%
// fillrate cut and the frame budget. The half-res pass is exercised (upload +
// ray-free FBM draw into a 1/4-size target); the golden uses the matching
// full-res path so pixel parity is checked against the same shader.
#include <doctest/doctest.h>

#include "stb_image.h"
#include "stb_image_write.h"

#include "environment/nebula_optimized.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
constexpr int kTolerance = 14;

environment::NebulaOptContext demoContext()
{
    environment::NebulaOptContext ctx;
    ctx.right = {1.0f, 0.0f, 0.0f, 0.0f};
    ctx.up = {0.0f, 1.0f, 0.0f, 0.0f};
    ctx.forward = {0.0f, 0.0f, 1.0f, 0.0f};
    ctx.aspectTan = {1.0f, 1.0f};
    return ctx;
}
environment::NebulaOptParams demoParams()
{
    environment::NebulaOptParams params;
    params.density = 1.0f;
    params.scattering = 0.9f;
    params.absorption = 0.1f;
    params.color = {0.35f, 0.18f, 0.6f, 1.0f};
    return params;
}
} // namespace

TEST_CASE("M8 residual: half-res 3D-noise nebula golden, parity and budget (T3)")
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

    auto nebula = environment::NebulaOptimized::create({
        .instance = context->instance(), .device = device->device(),
        .physicalDevice = context->physicalDevice(), .width = kW, .height = kH,
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = STARFORGE_SHADER_DIR,
        .seed = 42});
    REQUIRE(nebula.has_value());

    // >=75% fillrate cut: half-res is 1/2 x 1/2 = 1/4 the pixels.
    CHECK(nebula->halfResWidth() == kW / 2);
    CHECK(nebula->halfResHeight() == kH / 2);
    CHECK(static_cast<double>(nebula->halfResWidth()) * nebula->halfResHeight() <=
          0.25 * kW * kH);

    auto target = Image::create(*allocator, {
        .width = kW, .height = kH, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc});
    REQUIRE(target.has_value());
    auto targetView = target->createImageView(device->device());
    REQUIRE(targetView.has_value());
    auto readback = Buffer::create(*allocator, {
        .size = kPixelBytes, .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

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

    // Records both paths: the half-res offscreen FBM draw (1/4 pixels) and a
    // full-res draw of the same shader for the golden + parity comparison.
    auto record = [&]() {
        REQUIRE(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
        nebula->renderLowRes(cmd, demoContext(), demoParams());
        target->transitionToColorAttachment(cmd);
        nebula->renderFullRes(cmd, demoContext(), demoParams(), targetView->handle(), kW, kH);
        target->transitionToTransferSrc(cmd);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {kW, kH, 1};
        vkCmdCopyImageToBuffer(cmd, target->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
    auto readPixels = [&](std::vector<std::uint8_t>& out) {
        auto mapped = readback->map();
        REQUIRE(mapped.has_value());
        (void)readback->invalidate(0, kPixelBytes);
        out.assign(static_cast<const std::uint8_t*>(*mapped),
                   static_cast<const std::uint8_t*>(*mapped) + kPixelBytes);
        readback->unmap();
    };

    record();
    submit();
    std::vector<std::uint8_t> pixels, repeat;
    readPixels(pixels);

    // Golden for the 3D-noise nebula.
    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/nebula_noise3d_128.png";
    if (std::getenv("STARFORGE_UPDATE_GOLDENS") != nullptr)
    {
        REQUIRE(stbi_write_png(goldenPath.c_str(), kW, kH, 4, pixels.data(), kW * 4) == 1);
    }
    else
    {
        int gw = 0, gh = 0, gc = 0;
        unsigned char* golden = stbi_load(goldenPath.c_str(), &gw, &gh, &gc, 4);
        REQUIRE_MESSAGE(golden != nullptr, "Golden missing: " << goldenPath);
        std::uint32_t failing = 0;
        for (std::size_t i = 0; i < kPixelBytes; ++i)
            if (std::abs(static_cast<int>(pixels[i]) - static_cast<int>(golden[i])) > kTolerance)
                ++failing;
        stbi_image_free(golden);
        CHECK(static_cast<double>(failing) / kPixelBytes < 0.01);
    }

    // Pixel parity: a second render with the same seed is bit-identical.
    record();
    submit();
    readPixels(repeat);
    CHECK(pixels == repeat);

    std::size_t lit = 0;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
        if (pixels[i] + pixels[i + 1] + pixels[i + 2] > 20) ++lit;
    CHECK(lit > 200); // seeded volume produces visible nebula structure

    // Budget: p95 of both paths stays under the ADR-004 interval.
    std::vector<double> timings;
    for (int i = 0; i < 14; ++i)
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
