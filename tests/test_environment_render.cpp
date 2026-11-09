#include <doctest/doctest.h>

#include "environment/afterburner_effect.hpp"
#include "environment/collision_effect.hpp"
#include "environment/dust_field.hpp"
#include "environment/ftl_effect.hpp"
#include "environment/nebula_renderer.hpp"
#include "environment/particle_system.hpp"
#include "environment/shield_effect.hpp"
#include "environment/storm_effect.hpp"
#include "environment/sun_distortion.hpp"
#include "environment/thruster_effect.hpp"
#include "environment/weapon_effect.hpp"
#include "environment/wormhole_effect.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::math;
using namespace starforge::vulkan;

namespace
{

constexpr std::uint32_t kW = 128;
constexpr std::uint32_t kH = 128;

// Renders one effect into an offscreen image via the given record callback
// (runs inside the rendering pass), then returns the lit-pixel count.
// All driver calls are REQUIRE-checked (no silent hangs on failure).
std::uint32_t renderAndCountLit(Allocator& allocator, VulkanDevice& device,
                                const std::function<void(VkCommandBuffer cmd)>& record)
{
    auto colorImage = Image::create(allocator, {
        .width = kW,
        .height = kH,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsageFlags::ColorAttachment,
    });
    REQUIRE(colorImage.has_value());
    auto colorView = colorImage->createView(device.device());
    REQUIRE(colorView.has_value());
    auto readback = Buffer::create(allocator, {
        .size = kW * kH * 4,
        .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu,
    });
    REQUIRE(readback.has_value());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device.graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    REQUIRE(vkCreateCommandPool(device.device(), &poolInfo, nullptr, &cmdPool) == VK_SUCCESS);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(device.device(), &allocInfo, &cmd) == VK_SUCCESS);

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
    renderingInfo.renderArea.extent = {kW, kH};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

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

    record(cmd);

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

    REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    REQUIRE(vkCreateFence(device.device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = cmd;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;
    REQUIRE(vkQueueSubmit2(device.graphicsQueue(), 1, &submitInfo, fence) == VK_SUCCESS);
    REQUIRE(vkWaitForFences(device.device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

    auto mapped = readback->map();
    REQUIRE(mapped.has_value());
    (void)readback->invalidate(0, kW * kH * 4);
    const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
    std::uint32_t lit = 0;
    for (std::size_t i = 0; i < kW * kH * 4; i += 4)
    {
        const auto lum = static_cast<std::uint32_t>(bytes[i]) + bytes[i + 1] + bytes[i + 2];
        if (lum > 8)
        {
            ++lit;
        }
    }
    readback->unmap();

    vkDestroyFence(device.device(), fence, nullptr);
    vkDestroyImageView(device.device(), *colorView, nullptr);
    vkFreeCommandBuffers(device.device(), cmdPool, 1, &cmd);
    vkDestroyCommandPool(device.device(), cmdPool, nullptr);
    colorImage = std::expected<Image, std::string>();
    readback = std::expected<Buffer, std::string>();
    return lit;
}

Mat4f testViewProj()
{
    glm::mat4 view = glm::lookAt(Vec3f{0.0f, 0.0f, 5.0f}, Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{0.0f, 1.0f, 0.0f});
    glm::mat4 proj = perspectiveVulkan(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    return proj * view;
}

} // namespace

TEST_CASE("Environment render: all effects on a shared device")
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

    const std::string dir = std::string(STARFORGE_SHADER_DIR);

    SUBCASE("nebula")
    {
        NebulaRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.invViewProj = glm::inverse(ctx.viewProj);
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        NebulaParams params;
        params.time = 1.0f;
        auto eff = NebulaRenderer::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    SUBCASE("dust field")
    {
        DustFieldRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.invViewProj = glm::inverse(ctx.viewProj);
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        DustFieldParams params;
        params.time = 1.0f;
        auto eff = DustFieldRenderer::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    SUBCASE("sun distortion")
    {
        SunDistortionRenderContext ctx;
        ctx.sunScreenPos = {0.5f, 0.5f};
        ctx.sunRadiusScreen = 0.2f;
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        SunDistortionParams params;
        params.time = 1.0f;
        auto eff = SunDistortionEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    SUBCASE("wormhole")
    {
        WormholeRenderContext ctx;
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        WormholeParams params;
        params.time = 1.0f;
        params.spin = 0.5f;
        auto eff = WormholeEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    SUBCASE("FTL streaks")
    {
        FtlRenderContext ctx;
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        FtlParams params;
        params.time = 1.0f;
        params.speed = 2.0f;
        auto eff = FtlEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    SUBCASE("storm clouds")
    {
        StormRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.invViewProj = glm::inverse(testViewProj());
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        StormParams params;
        params.time = 1.0f;
        auto eff = StormEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    SUBCASE("particles")
    {
        ParticleRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        ParticleParams params;
        params.emitterPosition = {0.0f, 0.0f, 0.0f};
        params.size = 0.8f;
        params.speed = 0.3f;
        params.count = 64;
        auto eff = ParticleSystem::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    SUBCASE("weapon beam")
    {
        WeaponRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        WeaponParams params;
        params.position = {0.0f, 0.0f, 0.0f};
        params.size = 0.6f;
        auto eff = WeaponEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 100);
    }

    SUBCASE("shield bubble")
    {
        ShieldRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        ShieldParams params;
        params.position = {0.0f, 0.0f, 0.0f};
        params.radius = 1.2f;
        auto eff = ShieldEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 100);
    }

    SUBCASE("thruster cone")
    {
        ThrusterRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        ThrusterParams params;
        params.position = {0.0f, 0.0f, 0.0f};
        params.width = 0.6f;
        auto eff = ThrusterEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    SUBCASE("collision shockwave")
    {
        CollisionRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        CollisionParams params;
        params.position = {0.0f, 0.0f, 0.0f};
        params.radius = 0.8f;
        params.time = 0.5f;
        auto eff = CollisionEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 100);
    }

    SUBCASE("afterburner cone")
    {
        AfterburnerRenderContext ctx;
        ctx.viewProj = testViewProj();
        ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
        AfterburnerParams params;
        params.position = {0.0f, 0.0f, 0.0f};
        params.width = 0.6f;
        params.time = 1.0f;
        auto eff = AfterburnerEffect::create({
            .device = device->device(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .shaderDirectory = dir,
        });
        REQUIRE(eff.has_value());
        auto lit = renderAndCountLit(*allocator, *device, [&](VkCommandBuffer cmd) {
            eff->render(cmd, ctx, params);
        });
        CHECK(lit > 200);
    }

    CHECK(context->validationErrorCount() == 0);
}
