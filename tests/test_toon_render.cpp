// Toon rendering tests (M9.5.1/9.5.2): quantized bands, stepped specular,
// rim light, and inverted-hull outlines, proven by offscreen readback.
#include <doctest/doctest.h>

#include "environment/toon_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;
namespace math = starforge::math;

namespace
{

struct Pixel
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

// Renders the toon cube once with the given view-space light direction and
// returns the 64x64 readback pixels (row-major).
[[nodiscard]] std::vector<Pixel> renderToonCube(const math::Vec3f& lightDirectionView)
{
    constexpr std::uint32_t kW = 64;
    constexpr std::uint32_t kH = 64;

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

    auto toon = ToonRenderer::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = depthFormat,
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    REQUIRE(toon.has_value());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &cmdPool) == VK_SUCCESS);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(device->device(), &allocInfo, &cmd) == VK_SUCCESS);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);

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

    ToonRenderContext renderContext;
    // Camera at (0,0,5) looking at the origin; cube (half extent 0.5) at
    // the origin spans about 26..38 px on the 64px target.
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    renderContext.modelView = view;
    renderContext.viewProj = projection * view;
    renderContext.lightDirectionView = lightDirectionView;
    renderContext.resolution = math::Vec2f{64.0f, 64.0f};

    ToonParams params;
    params.baseColor = math::Vec4f{0.5f, 0.25f, 0.2f, 1.0f};
    params.shadowColor = math::Vec4f{0.25f, 0.05f, 0.1f, 1.0f};
    params.bandCount = 3.0f;
    params.rimStrength = 0.0f; // keep band assertions exact
    params.outlineWidth = 2.0f; // pixels

    toon->render(cmd, renderContext, params);

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
    REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = cmd;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;
    REQUIRE(vkQueueSubmit2(device->graphicsQueue(), 1, &submitInfo, fence) == VK_SUCCESS);
    REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

    auto mapped = readback->map();
    REQUIRE(mapped.has_value());
    (void)readback->invalidate(0, kW * kH * 4);

    const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
    std::vector<Pixel> pixels(kW * kH);
    std::memcpy(pixels.data(), bytes, kW * kH * sizeof(Pixel));
    readback->unmap();

    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
    vkDestroyCommandPool(device->device(), cmdPool, nullptr);
    vkDestroyImageView(device->device(), *colorView, nullptr);
    vkDestroyImageView(device->device(), *depthView, nullptr);

    CHECK(context->validationErrorCount() == 0);
    return pixels;
}

[[nodiscard]] Pixel at(const std::vector<Pixel>& pixels, std::uint32_t x, std::uint32_t y)
{
    return pixels[y * 64 + x];
}

[[nodiscard]] bool nearChannel(std::uint8_t actual, float expected)
{
    return std::abs(static_cast<float>(actual) - expected * 255.0f) <= 8.0f;
}

} // namespace

TEST_CASE("Toon rendering: quantized bands and inverted-hull outline")
{
    // Frontal light: the front face (normal +Z) takes the top band.
    // Top band = baseColor + stepped specular (0.7 * baseColor), i.e.
    // (0.5, 0.25, 0.2) * 1.7 = (0.85, 0.425, 0.34) before clamping.
    const auto frontal = renderToonCube(math::Vec3f{0.0f, 0.0f, 1.0f});
    REQUIRE(frontal.size() == 64 * 64);

    const Pixel centre = at(frontal, 32, 32);
    CHECK(nearChannel(centre.r, 0.85f));
    CHECK(nearChannel(centre.g, 0.425f));
    CHECK(nearChannel(centre.b, 0.34f));

    // Outline: scan the middle row for the filled cube's left edge; the
    // pixel just outside it must be the flat dark outline color, and a
    // pixel further out must be clear black.
    std::uint32_t fillEdge = 64;
    for (std::uint32_t x = 0; x < 64; ++x)
    {
        // Filled pixels have a strong red channel; outline and clear do not.
        if (at(frontal, x, 32).r > 100)
        {
            fillEdge = x;
            break;
        }
    }
    REQUIRE(fillEdge > 2);
    REQUIRE(fillEdge < 32);
    const Pixel outline = at(frontal, fillEdge - 1, 32);
    CHECK(nearChannel(outline.r, 0.03f));
    CHECK(nearChannel(outline.g, 0.03f));
    CHECK(nearChannel(outline.b, 0.05f));
    const Pixel outside = at(frontal, fillEdge - 8, 32);
    CHECK(outside.r == 0);
    CHECK(outside.g == 0);
    CHECK(outside.b == 0);

    const Pixel corner = at(frontal, 0, 0);
    CHECK(corner.r == 0);
    CHECK(corner.g == 0);
    CHECK(corner.b == 0);

    // Grazing light: the front face (normal perpendicular to the light)
    // falls to the shadow band with no specular or rim contribution.
    const auto grazing = renderToonCube(math::Vec3f{1.0f, 0.0f, 0.0f});
    const Pixel shadowed = at(grazing, 32, 32);
    CHECK(nearChannel(shadowed.r, 0.25f));
    CHECK(nearChannel(shadowed.g, 0.05f));
    CHECK(nearChannel(shadowed.b, 0.1f));
}
