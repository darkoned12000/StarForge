#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_renderer.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::gpu_resources;
using namespace starforge::math;
using namespace starforge::vulkan;

namespace
{

std::vector<char> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) return {};
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VkShaderModule loadShader(VkDevice device, const std::string& path)
{
    auto spv = readFile(path);
    if (spv.empty()) return VK_NULL_HANDLE;

    std::vector<uint32_t> aligned(spv.size() / sizeof(uint32_t));
    std::memcpy(aligned.data(), spv.data(), spv.size());

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = aligned.data();

    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

// Render two overlapping quads at different depths in a given draw order.
// nearColor and farColor are the colors of the near and far surfaces.
// drawNearFirst: if true, near surface is drawn first, then far.
// Returns the dominant color (which surface won the depth test).
struct RenderResult
{
    std::uint8_t r, g, b;
};

RenderResult renderDepthOrder(VkDevice device, VkPhysicalDevice physDevice,
                              std::uint32_t queueFamily,
                              Allocator& allocator, VkQueue queue,
                              bool drawNearFirst)
{
    constexpr std::uint32_t w = 32, h = 32;
    constexpr VkDeviceSize byteSize = w * h * 4;

    // Create color image.
    auto colorImage = Image::create(allocator, {
        .width = w, .height = h,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsage::ColorAttachment,
    });

    auto colorView = colorImage->createView(device);

    // Create depth image.
    VkFormat depthFormat = findDepthFormat(physDevice);
    auto depthImage = Image::create(allocator, {
        .width = w, .height = h,
        .format = depthFormat,
        .usage = ImageUsage::DepthAttachment,
    });
    auto depthView = depthImage->createView(device, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Readback buffer.
    auto readback = Buffer::create(allocator, {
        .size = byteSize,
        .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu,
    });

    // Load shaders.
    std::string shaderDir = std::string(STARFORGE_SHADER_DIR);
    VkShaderModule vertModule = loadShader(device, shaderDir + "/test_depth.vert.spv");
    VkShaderModule fragModule = loadShader(device, shaderDir + "/test_depth.frag.spv");

    // Pipeline layout: push constants = MVP(64) + color(12) + depth(4) = 80 bytes.
    auto pipelineLayout = createPushConstantPipelineLayout(device, 80);

    // Graphics pipeline with reverse-Z depth test.
    auto pipeline = GraphicsPipeline::create({
        .device = device,
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = depthFormat,
        .vertexShader = vertModule,
        .fragmentShader = fragModule,
        .pipelineLayout = *pipelineLayout,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_GREATER,
        .backfaceCulling = false,
        .viewportWidth = static_cast<float>(w),
        .viewportHeight = static_cast<float>(h),
    });

    // Command buffer.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    Image::transitionToColorAttachment(cmd, colorImage->handle());
    Image::transitionToDepthAttachment(cmd, depthImage->handle(), depthFormat);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = *colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearColorValue clearColor{{0.0f, 0.0f, 0.0f, 1.0f}};
    colorAttachment.clearValue.color = clearColor;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = *depthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearDepthStencilValue clearDepth{0.0f, 0};
    depthAttachment.clearValue.depthStencil = clearDepth;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {w, h};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(w);
    viewport.height = static_cast<float>(h);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {w, h};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Push constants struct (must match shader layout).
    struct PushData {
        float mvpRow0[4];
        float mvpRow1[4];
        float mvpRow2[4];
        float mvpRow3[4];
        float colorAndDepth[4]; // rgb = color, a = depth (NDC z)
    };

    // Identity MVP (vertices already in clip space).
    PushData nearPush{};
    for (int i = 0; i < 4; ++i) { nearPush.mvpRow0[i] = (i == 0) ? 1.0f : 0.0f; }
    for (int i = 0; i < 4; ++i) { nearPush.mvpRow1[i] = (i == 1) ? 1.0f : 0.0f; }
    for (int i = 0; i < 4; ++i) { nearPush.mvpRow2[i] = (i == 2) ? 1.0f : 0.0f; }
    for (int i = 0; i < 4; ++i) { nearPush.mvpRow3[i] = (i == 3) ? 1.0f : 0.0f; }
    nearPush.colorAndDepth[0] = 1.0f; nearPush.colorAndDepth[1] = 0.0f; nearPush.colorAndDepth[2] = 0.0f; // red = near
    nearPush.colorAndDepth[3] = 0.5f; // near surface (higher depth = closer in reverse-Z)

    PushData farPush{};
    for (int i = 0; i < 4; ++i) { farPush.mvpRow0[i] = (i == 0) ? 1.0f : 0.0f; }
    for (int i = 0; i < 4; ++i) { farPush.mvpRow1[i] = (i == 1) ? 1.0f : 0.0f; }
    for (int i = 0; i < 4; ++i) { farPush.mvpRow2[i] = (i == 2) ? 1.0f : 0.0f; }
    for (int i = 0; i < 4; ++i) { farPush.mvpRow3[i] = (i == 3) ? 1.0f : 0.0f; }
    farPush.colorAndDepth[0] = 0.0f; farPush.colorAndDepth[1] = 0.0f; farPush.colorAndDepth[2] = 1.0f; // blue = far
    farPush.colorAndDepth[3] = 0.1f; // far surface (lower depth = farther in reverse-Z)

    // Draw in the specified order.
    auto drawQuad = [&](const PushData& pd) {
        vkCmdPushConstants(cmd, *pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pd), &pd);
        pipeline->bindAndDraw(cmd, 6, 0, 1);
    };

    if (drawNearFirst)
    {
        drawQuad(nearPush);
        drawQuad(farPush);
    }
    else
    {
        drawQuad(farPush);
        drawQuad(nearPush);
    }

    vkCmdEndRendering(cmd);

    // Transition and readback.
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
    region.imageExtent = {w, h, 1};
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

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device, &fenceInfo, nullptr, &fence);

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;
    vkQueueSubmit2(queue, 1, &submitInfo, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    auto mapped = readback->map();
    (void)readback->invalidate(0, byteSize);
    const auto* bytes = static_cast<const std::uint8_t*>(*mapped);

    // Check the center pixel color.
    std::size_t centerIdx = (h / 2 * w + w / 2) * 4;
    RenderResult result{bytes[centerIdx], bytes[centerIdx + 1], bytes[centerIdx + 2]};

    readback->unmap();

    // Cleanup.
    vkDestroyFence(device, fence, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyPipelineLayout(device, *pipelineLayout, nullptr);
    vkDestroyImageView(device, *colorView, nullptr);
    vkDestroyImageView(device, *depthView, nullptr);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    vkDestroyCommandPool(device, cmdPool, nullptr);

    return result;
}

} // namespace

TEST_CASE("Depth ordering: reverse-Z with two overlapping surfaces")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());

        {
            // Test both draw orders.
            auto resultNearFirst = renderDepthOrder(device->device(), context->physicalDevice(),
                                                   device->graphicsQueueFamily(), *allocator, device->graphicsQueue(), true);
            auto resultFarFirst = renderDepthOrder(device->device(), context->physicalDevice(),
                                                  device->graphicsQueueFamily(), *allocator, device->graphicsQueue(), false);

            // In both cases, the near surface (red) should win the depth test.
            CHECK(resultNearFirst.r > 200); // red channel dominant
            CHECK(resultNearFirst.g < 50);
            CHECK(resultNearFirst.b < 50);

            CHECK(resultFarFirst.r > 200);
            CHECK(resultFarFirst.g < 50);
            CHECK(resultFarFirst.b < 50);

            CHECK(allocator->allocationCount() == 0);
        }

        CHECK(context->validationErrorCount() == 0);
    }
}
