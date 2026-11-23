// M17.5d T3 proof: faction-coloured ships remain distinguishable at 50 m
// and 2 km. The hull, graphic ink outline and HDR nav glow all come from
// the same relation palette used by the HUD and radar.
#include <doctest/doctest.h>

#include "environment/toon_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_shader_specialization.hpp"

#include "stb_image.h"
#include "stb_image_write.h"

#include <array>
#include <cmath>
#include <cstdlib>
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

struct Vertex
{
    math::Vec4f position{};
    math::Vec4f normal{};
    math::Vec4f outline{};
};

// Simple faceted fighter: a wedge hull and two swept wings, nose down -Z.
std::vector<environment::ToonVertex> makeShip()
{
    const std::array<math::Vec3f, 7> p{{
        {0.0f, 0.0f, -1.8f},   // nose
        {-0.5f, 0.3f, 0.9f},   // top-left
        {0.5f, 0.3f, 0.9f},    // top-right
        {-0.6f, -0.2f, 0.8f},  // bottom-left
        {0.6f, -0.2f, 0.8f},   // bottom-right
        {-1.7f, 0.0f, 0.6f},   // wing-left
        {1.7f, 0.0f, 0.6f},    // wing-right
    }};
    const std::array<std::array<std::uint32_t, 3>, 8> faces{{
        {{0, 1, 2}}, {{0, 4, 3}}, {{0, 3, 1}}, {{0, 2, 4}},
        {{1, 3, 5}}, {{2, 6, 4}}, {{1, 5, 6}}, {{2, 4, 6}},
    }};
    std::vector<environment::ToonVertex> out;
    for (const auto& face : faces)
    {
        const auto& a = p[face[0]];
        const auto& b = p[face[1]];
        const auto& c = p[face[2]];
        const auto e1 = b - a;
        const auto e2 = c - a;
        const auto n = glm::normalize(glm::cross(e1, e2));
        for (const auto i : face)
            out.push_back({p[i], n, glm::normalize(p[i])});
    }
    return out;
}

[[nodiscard]] bool renderFaction(const char* goldenName, float distance,
                                 const math::Vec4f& hull, const math::Vec4f& ink,
                                 const math::Vec4f& glow, std::size_t& lit)
{
    ContextCreateInfo info;
    info.enableValidation = true;
    info.requiredExtensions.clear();
    auto context = VulkanContext::create(info);
    if (!context) return false;
    auto device = VulkanDevice::create(*context);
    if (!device) return false;
    auto allocator = Allocator::create({.instance = context->instance(),
                                        .physicalDevice = context->physicalDevice(),
                                        .device = device->device()});
    if (!allocator) return false;
    const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    const VkFormat depthFormat = findDepthFormat(context->physicalDevice());

    auto color = Image::create(*allocator, {.width = kW, .height = kH, .format = colorFormat,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc});
    if (!color) return false;
    auto colorView = color->createImageView(device->device());
    if (!colorView) return false;
    auto depth = Image::create(*allocator, {.width = kW, .height = kH, .format = depthFormat,
        .usage = ImageUsageFlags::DepthAttachment});
    if (!depth) return false;
    auto depthView = depth->createImageView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!depthView) return false;
    auto readback = Buffer::create(*allocator, {.size = kPixelBytes,
        .usage = BufferUsage::TransferDst, .memory = MemoryPlacement::GpuToCpu});
    if (!readback) return false;

    auto toon = environment::ToonRenderer::create({
        .instance = context->instance(), .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(), .framesInFlight = 1,
        .colorFormat = colorFormat, .depthFormat = depthFormat,
        .shaderDirectory = STARFORGE_SHADER_DIR,
        .specializationInfo = &vulkan::hdrOutputSpecialization(false)});
    if (!toon) return false;
    auto mesh = toon->createMesh(makeShip());
    if (!mesh) return false;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device->device(), &poolInfo, nullptr, &pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device->device(), &alloc, &cmd) != VK_SUCCESS) return false;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) != VK_SUCCESS) return false;

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) return false;
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
    VkRect2D scissor{{0, 0}, {kW, kH}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // A small fighter at close range, a capital ship at long range: both
    // must stay legible with just a hull, an ink outline and a nav glow.
    const float scale = distance > 500.0f ? 60.0f : 4.0f;
    const auto eye = distance;
    const auto view = glm::lookAt(math::Vec3f{0.0f, 1.0f, eye},
                                  math::Vec3f{0.0f, 0.0f, 0.0f},
                                  math::Vec3f{0.0f, 1.0f, 0.0f});
    const auto proj = math::perspectiveVulkan(1.0471976f, 1.0f, 0.1f, 10000.0f);
    auto model = math::Mat4f(1.0f);
    model[0][0] = model[1][1] = model[2][2] = scale;
    environment::ToonRenderContext ctx;
    ctx.viewProj = proj * view * model;
    ctx.modelView = view * model;
    ctx.lightDirectionView = {0.4f, 0.5f, 0.75f};
    ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
    environment::ToonParams params;
    params.baseColor = hull;
    params.shadowColor = ink;
    params.emissiveColor = glow;
    params.capabilities = 1u;
    params.outlineWidth = 1.5f;
    toon->beginFrame();
    toon->renderMesh(cmd, ctx, params, *mesh);
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
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) return false;
    VkCommandBufferSubmitInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    sci.commandBuffer = cmd;
    VkSubmitInfo2 sub{};
    sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    sub.commandBufferInfoCount = 1;
    sub.pCommandBufferInfos = &sci;
    if (device->submit(1, &sub, fence) != VK_SUCCESS) return false;
    if (vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;

    auto mapped = readback->map();
    if (!mapped) return false;
    (void)readback->invalidate(0, kPixelBytes);
    const auto* actual = static_cast<const std::uint8_t*>(*mapped);
    const std::string path = std::string(STARFORGE_GOLDEN_DIR) + "/" + goldenName;
    if (std::getenv("STARFORGE_UPDATE_GOLDENS") != nullptr)
    {
        if (stbi_write_png(path.c_str(), kW, kH, 4, actual, kW * 4) != 1) return false;
    }
    else
    {
        int gw = 0, gh = 0, gc = 0;
        unsigned char* golden = stbi_load(path.c_str(), &gw, &gh, &gc, 4);
        if (!golden) return false;
        std::uint32_t failing = 0;
        for (std::size_t i = 0; i < kPixelBytes; ++i)
            if (std::abs(static_cast<int>(actual[i]) - static_cast<int>(golden[i])) > 14) ++failing;
        stbi_image_free(golden);
        if (static_cast<double>(failing) / kPixelBytes >= 0.01) return false;
    }
    lit = 0;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
        if (actual[i] + actual[i + 1] + actual[i + 2] > 40) ++lit;
    readback->unmap();

    if (context->validationErrorCount() != 0) return false;
    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
    return true;
}
} // namespace

TEST_CASE("M17.5d: faction hull, ink outline and nav glow stay readable at 50 m and 2 km")
{
    const math::Vec4f allyHull{0.25f, 0.85f, 1.0f, 1.0f};
    const math::Vec4f allyInk{0.055f, 0.19f, 0.22f, 1.0f};
    const math::Vec4f allyGlow{0.25f, 0.85f, 1.0f, 1.6f};
    const math::Vec4f hostileHull{1.0f, 0.35f, 0.16f, 1.0f};
    const math::Vec4f hostileInk{0.22f, 0.077f, 0.035f, 1.0f};
    const math::Vec4f hostileGlow{1.0f, 0.35f, 0.16f, 1.6f};

    std::size_t allyNear = 0, hostileNear = 0, allyFar = 0, hostileFar = 0;
    CHECK(renderFaction("faction_ally_50m_128.png", 50.0f,
                        allyHull, allyInk, allyGlow, allyNear));
    CHECK(renderFaction("faction_hostile_50m_128.png", 50.0f,
                        hostileHull, hostileInk, hostileGlow, hostileNear));
    CHECK(renderFaction("faction_ally_2km_128.png", 2000.0f,
                        allyHull, allyInk, allyGlow, allyFar));
    CHECK(renderFaction("faction_hostile_2km_128.png", 2000.0f,
                        hostileHull, hostileInk, hostileGlow, hostileFar));

    CHECK(allyNear > 40);
    CHECK(hostileNear > 40);
    CHECK(allyFar > 10);
    CHECK(hostileFar > 10);
}
