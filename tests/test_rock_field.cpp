// M15.0.8/15.0.9 instanced rock field: seeded mesh variety (T1) and a headless
// golden covering per-instance tint, a single instanced mesh draw, the
// distance-aware outline and the distant impostor path (T3).
#include <doctest/doctest.h>

#include "stb_image.h"
#include "stb_image_write.h"

#include "environment/asteroid_field.hpp"
#include "environment/rock_field_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
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
constexpr int kTolerance = 12;

struct RockInstance
{
    float posScale[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float tintWidth[4] = {0.55f, 0.45f, 0.35f, 0.02f};
    std::uint32_t seed = 0;
    // Dissolve factor at offset 52: 1 = fully visible, 0 = fully dissolved.
    // It has to match the shader's InstanceData exactly - the old padding
    // slot became the fade value, so leaving it at zero dissolved the whole
    // test field.
    float fade = 1.0f;
    float pad[2] = {0.0f, 0.0f};
};
static_assert(sizeof(RockInstance) == 64);

std::expected<std::unique_ptr<Buffer>, std::string>
upload(Allocator& allocator, const void* data, VkDeviceSize size, BufferUsage usage)
{
    auto buffer = Buffer::create(allocator, {
        .size = size, .usage = usage, .memory = MemoryPlacement::CpuToGpu});
    if (!buffer) return std::unexpected(buffer.error());
    auto mapped = buffer->map();
    if (!mapped) return std::unexpected(mapped.error());
    std::memcpy(*mapped, data, static_cast<std::size_t>(size));
    (void)buffer->flush();
    buffer->unmap();
    return std::make_unique<Buffer>(std::move(*buffer));
}
} // namespace

TEST_CASE("M15.0.8: seeded asteroid meshes are distinct variants (T1)")
{
    auto a = environment::generateAsteroidMesh(1.0f, 12, 16, 0);
    auto b = environment::generateAsteroidMesh(1.0f, 12, 16, 1);
    auto c = environment::generateAsteroidMesh(1.0f, 12, 16, 2);
    auto aAgain = environment::generateAsteroidMesh(1.0f, 12, 16, 0);
    REQUIRE_FALSE(a.vertices.empty());
    REQUIRE_FALSE(a.indices.empty());
    CHECK(a.vertices == aAgain.vertices);       // deterministic per seed
    CHECK(a.vertices != b.vertices);            // different seed -> different rock
    CHECK(b.vertices != c.vertices);
    CHECK(a.indices.size() == b.indices.size()); // shared topology -> one index buffer
}

TEST_CASE("M15.0.8/15.0.9: instanced rocks tint + outline + impostor golden (T3)")
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

    auto mesh = environment::generateAsteroidMesh(1.0f, 12, 16, 3);
    auto vertexBuffer = upload(*allocator, mesh.vertices.data(),
                               mesh.vertices.size() * sizeof(float), BufferUsage::Storage);
    REQUIRE(vertexBuffer.has_value());
    auto indexBuffer = upload(*allocator, mesh.indices.data(),
                              mesh.indices.size() * sizeof(std::uint32_t), BufferUsage::Index);
    REQUIRE(indexBuffer.has_value());

    // Near ring (full mesh + outline) with three palette tints.
    std::vector<RockInstance> near;
    const float tints[3][3] = {{0.75f, 0.45f, 0.30f}, {0.35f, 0.55f, 0.70f}, {0.55f, 0.70f, 0.35f}};
    for (int i = 0; i < 48; ++i)
    {
        const float angle = static_cast<float>(i) / 48.0f * 6.2831853f;
        const float radius = 16.0f + (i % 5) * 2.5f;
        RockInstance inst;
        inst.posScale[0] = std::cos(angle) * radius;
        inst.posScale[1] = std::sin(angle) * radius * 0.6f;
        inst.posScale[2] = std::sin(angle * 2.0f) * 4.0f;
        inst.posScale[3] = 2.0f + (i % 4) * 0.6f;
        const int palette = i % 3;
        inst.tintWidth[0] = tints[palette][0];
        inst.tintWidth[1] = tints[palette][1];
        inst.tintWidth[2] = tints[palette][2];
        inst.tintWidth[3] = 0.012f; // world-space outline width
        inst.seed = static_cast<std::uint32_t>(i + 1);
        near.push_back(inst);
    }
    // Far ring (impostor path).
    std::vector<RockInstance> far;
    for (int i = 0; i < 40; ++i)
    {
        const float angle = static_cast<float>(i) / 40.0f * 6.2831853f;
        RockInstance inst;
        inst.posScale[0] = std::cos(angle) * 60.0f;
        inst.posScale[1] = std::sin(angle) * 45.0f;
        inst.posScale[2] = -20.0f;
        inst.posScale[3] = 4.0f;
        inst.tintWidth[0] = 0.5f;
        inst.tintWidth[1] = 0.4f;
        inst.tintWidth[2] = 0.65f;
        inst.tintWidth[3] = 0.0f;
        inst.seed = static_cast<std::uint32_t>(100 + i);
        far.push_back(inst);
    }
    auto nearBuffer = upload(*allocator, near.data(), near.size() * sizeof(RockInstance), BufferUsage::Storage);
    REQUIRE(nearBuffer.has_value());
    auto farBuffer = upload(*allocator, far.data(), far.size() * sizeof(RockInstance), BufferUsage::Storage);
    REQUIRE(farBuffer.has_value());

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

    auto field = environment::RockFieldRenderer::create({
        .device = device->device(), .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = depthFormat, .shaderDirectory = STARFORGE_SHADER_DIR});
    REQUIRE(field.has_value());

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

    const auto view = glm::lookAt(math::Vec3f{0.0f, 0.0f, 45.0f}, math::Vec3f{0.0f, 0.0f, 0.0f},
                                  math::Vec3f{0.0f, 1.0f, 0.0f});
    const auto proj = math::perspectiveVulkan(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);
    environment::RockRenderContext rctx;
    rctx.viewProj = proj * view;
    rctx.projectionScale = math::Vec2f{proj[0][0], proj[1][1]};
    rctx.resolution = math::Vec2f{static_cast<float>(kW), static_cast<float>(kH)};
    rctx.sunDirection = glm::normalize(math::Vec3f{0.4f, 0.6f, 1.0f});
    environment::RockDrawParams rparams;
    rparams.vertexAddress = (*vertexBuffer)->deviceAddress(device->device());
    rparams.nearInstanceAddress = (*nearBuffer)->deviceAddress(device->device());
    rparams.nearInstanceCount = static_cast<std::uint32_t>(near.size());
    rparams.farInstanceAddress = (*farBuffer)->deviceAddress(device->device());
    rparams.farInstanceCount = static_cast<std::uint32_t>(far.size());
    rparams.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    rparams.outline = true;
    vkCmdBindIndexBuffer(cmd, (*indexBuffer)->handle(), 0, VK_INDEX_TYPE_UINT32);
    field->render(cmd, rctx, rparams);
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
    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/rock_field_128.png";
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
    bool warm = false, cool = false;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
    {
        if (actual[i] + actual[i + 1] + actual[i + 2] > 40) ++lit;
        if (actual[i] > actual[i + 2] + 20) warm = true;   // rust-tinted rocks
        if (actual[i + 2] > actual[i] + 20) cool = true;   // blue/green-tinted rocks
    }
    readback->unmap();
    CHECK(lit > 800);   // dense field fills the frame
    CHECK(warm);        // varied palette: warm rocks present
    CHECK(cool);        // varied palette: cool rocks present
    CHECK(context->validationErrorCount() == 0);

    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
}
