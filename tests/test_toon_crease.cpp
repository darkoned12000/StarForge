// M9.5 screen-space crease/outline: G-buffer (object ID + normals + depth),
// reverse-Z depth linearization and an edge-detect post pass.
#include <doctest/doctest.h>

#include "stb_image.h"
#include "stb_image_write.h"

#include "environment/internal/texture_effect.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
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
constexpr int kTolerance = 10;

VkShaderModule loadModule(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) return VK_NULL_HANDLE;
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<std::uint32_t> words(size / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

struct GBufferPush
{
    math::Mat4f viewProj{};
    math::Mat4f modelView{1.0f};
    std::uint32_t objectId = 0;
    float pad0 = 0.0f, pad1 = 0.0f, pad2 = 0.0f;
};
static_assert(sizeof(GBufferPush) == 144, "GBufferPush must match gbuffer.slang");

struct EdgePush
{
    float nearPlane = 0.1f, farPlane = 100.0f, idThreshold = 0.5f, depthThreshold = 0.02f;
    float width = static_cast<float>(kW), height = static_cast<float>(kH), pad0 = 0.0f, pad1 = 0.0f;
};
static_assert(sizeof(EdgePush) == 32);

} // namespace

TEST_CASE("M9.5: screen-space crease pass golden (T3)")
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

    auto gColor = Image::create(*allocator, {
        .width = kW, .height = kH, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::Sampled});
    REQUIRE(gColor.has_value());
    auto gColorView = gColor->createImageView(device->device());
    REQUIRE(gColorView.has_value());

    auto gDepth = Image::create(*allocator, {
        .width = kW, .height = kH, .format = depthFormat,
        .usage = ImageUsageFlags::DepthAttachment | ImageUsageFlags::Sampled});
    REQUIRE(gDepth.has_value());
    auto gDepthView = gDepth->createImageView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
    REQUIRE(gDepthView.has_value());

    auto output = Image::create(*allocator, {
        .width = kW, .height = kH, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc});
    REQUIRE(output.has_value());
    auto outputView = output->createImageView(device->device());
    REQUIRE(outputView.has_value());
    auto readback = Buffer::create(*allocator, {
        .size = kPixelBytes, .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

    auto vertexModule = loadModule(device->device(), std::string(STARFORGE_SHADER_DIR) + "/gbuffer.vert.spv");
    auto fragmentModule = loadModule(device->device(), std::string(STARFORGE_SHADER_DIR) + "/gbuffer.frag.spv");
    REQUIRE(vertexModule != VK_NULL_HANDLE);
    REQUIRE(fragmentModule != VK_NULL_HANDLE);
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(GBufferPush);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    REQUIRE(vkCreatePipelineLayout(device->device(), &layoutInfo, nullptr, &pipelineLayout) == VK_SUCCESS);

    auto gbufferPipeline = GraphicsPipeline::create({
        .device = device->device(),
        .colorFormat = VK_FORMAT_R32G32B32A32_SFLOAT,
        .depthFormat = depthFormat,
        .vertexShader = vertexModule,
        .fragmentShader = fragmentModule,
        .pipelineLayout = pipelineLayout,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_GREATER, // reverse-Z
        .backfaceCulling = true,
        .viewportWidth = static_cast<float>(kW),
        .viewportHeight = static_cast<float>(kH),
    });
    REQUIRE(gbufferPipeline.has_value());

    auto edge = starforge::environment::internal::TextureEffect::create({
        .device = device->device(), .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .shaderDirectory = STARFORGE_SHADER_DIR, .shaderBaseName = "edge_detect",
        .pushConstantSize = sizeof(EdgePush)});
    REQUIRE(edge.has_value());

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

    gColor->transitionToColorAttachment(cmd);
    Image::transitionLayout(cmd, gDepth->handle(), VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = gColorView->handle();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = gDepthView->handle();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {0.0f, 0}; // reverse-Z clear
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

    const math::Mat4f view = glm::lookAt(math::Vec3f{0, 0, 6}, math::Vec3f{0, 0, 0}, math::Vec3f{0, 1, 0});
    const math::Mat4f viewProj = math::perspectiveVulkanReverseZ(glm::radians(55.0f), 1.0f, 0.1f, 100.0f) * view;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline->handle());
    const float xs[3] = {-1.1f, 0.0f, 1.1f};
    const float ds[3] = {-2.0f, 0.0f, 2.0f};
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        GBufferPush push;
        push.viewProj = viewProj;
        push.modelView = glm::translate(math::Mat4f{1.0f}, math::Vec3f{xs[i], 0.0f, ds[i]});
        push.objectId = i + 1;
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 36, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);

    gColor->transitionToShaderReadOnly(cmd);
    Image::transitionLayout(cmd, gDepth->handle(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT,
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

    output->transitionToColorAttachment(cmd);
    VkRenderingAttachmentInfo outputAttachment{};
    outputAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    outputAttachment.imageView = outputView->handle();
    outputAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    outputAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    outputAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    outputAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingInfo outputRendering{};
    outputRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    outputRendering.renderArea.extent = {kW, kH};
    outputRendering.layerCount = 1;
    outputRendering.colorAttachmentCount = 1;
    outputRendering.pColorAttachments = &outputAttachment;
    vkCmdBeginRendering(cmd, &outputRendering);
    EdgePush edgePush;
    edge->render(cmd, gColorView->handle(), &edgePush, sizeof(edgePush), gDepthView->handle());
    vkCmdEndRendering(cmd);

    output->transitionToTransferSrc(cmd);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {kW, kH, 1};
    vkCmdCopyImageToBuffer(cmd, output->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->handle(), 1, &copy);
    VkBufferMemoryBarrier2 hostBarrier{};
    hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    hostBarrier.buffer = readback->handle();
    hostBarrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &hostBarrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
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
    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/toon_crease_128.png";
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
    std::size_t dark = 0, light = 0;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
    {
        if (actual[i] < 64 && actual[i + 1] < 64) ++dark;
        if (actual[i] > 192 && actual[i + 1] > 192) ++light;
    }
    readback->unmap();
    CHECK(dark > 100);  // silhouette / crease lines are visible
    CHECK(light > 100); // non-edge background remains
    CHECK(context->validationErrorCount() == 0);

    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
    vkDestroyPipelineLayout(device->device(), pipelineLayout, nullptr);
    vkDestroyShaderModule(device->device(), vertexModule, nullptr);
    vkDestroyShaderModule(device->device(), fragmentModule, nullptr);
}
