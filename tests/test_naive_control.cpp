#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "spatial/camera_relative.hpp"
#include "spatial/render_object.hpp"
#include "spatial/world_position.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_renderer.hpp"

#include <cmath>
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
using namespace starforge::spatial;
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

// Render a single triangle at a given camera-relative transform and read back.
// Returns the number of non-zero (colored) pixels.
struct RenderStats
{
    std::uint32_t nonZeroPixels = 0;
    std::uint32_t totalPixels = 0;
};

RenderStats renderTriangle(VkDevice device, VkPhysicalDevice physDevice,
                          std::uint32_t queueFamily, Allocator& allocator,
                          VkQueue queue, const Mat4f& mvp)
{
    constexpr std::uint32_t w = 32, h = 32;
    constexpr VkDeviceSize byteSize = w * h * 4;

    auto colorImage = Image::create(allocator, {
        .width = w, .height = h,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsage::ColorAttachment,
    });

    auto colorView = colorImage->createView(device);

    VkFormat depthFormat = findDepthFormat(physDevice);
    auto depthImage = Image::create(allocator, {
        .width = w, .height = h,
        .format = depthFormat,
        .usage = ImageUsage::DepthAttachment,
    });
    auto depthView = depthImage->createView(device, VK_IMAGE_ASPECT_DEPTH_BIT);

    auto readback = Buffer::create(allocator, {
        .size = byteSize,
        .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu,
    });

    std::string shaderDir = std::string(STARFORGE_SHADER_DIR);
    VkShaderModule vertModule = loadShader(device, shaderDir + "/test_triangle.vert.spv");
    VkShaderModule fragModule = loadShader(device, shaderDir + "/test_triangle.frag.spv");

    auto pipelineLayout = createPushConstantPipelineLayout(device, 64);
    auto pipeline = GraphicsPipeline::create({
        .device = device,
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = depthFormat,
        .vertexShader = vertModule,
        .fragmentShader = fragModule,
        .pipelineLayout = *pipelineLayout,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = false,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_ALWAYS,
        .backfaceCulling = false,
        .viewportWidth = static_cast<float>(w),
        .viewportHeight = static_cast<float>(h),
    });

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
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

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = *colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearColorValue clearColor{{0.0f, 0.0f, 0.0f, 1.0f}};
    colorAttachment.clearValue.color = clearColor;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {w, h};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0f; viewport.y = 0.0f;
    viewport.width = static_cast<float>(w); viewport.height = static_cast<float>(h);
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0}; scissor.extent = {w, h};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    struct PushData { float mvp[16]; } pushData{};
    const float* mvpData = glm::value_ptr(mvp);
    for (int i = 0; i < 16; ++i) pushData.mvp[i] = mvpData[i];

    vkCmdPushConstants(cmd, *pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pushData), &pushData);
    pipeline->bindAndDraw(cmd, 3, 0, 1);

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

    RenderStats stats{};
    stats.totalPixels = w * h;
    for (std::uint32_t i = 0; i < w * h; ++i)
    {
        if (bytes[i * 4] != 0 || bytes[i * 4 + 1] != 0 || bytes[i * 4 + 2] != 0)
        {
            stats.nonZeroPixels++;
        }
    }

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

    // Release allocations.
    colorImage = std::expected<Image, std::string>();
    depthImage = std::expected<Image, std::string>();
    readback = std::expected<Buffer, std::string>();

    return stats;
}

} // namespace

TEST_CASE("Naive control: error-vs-distance table")
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

        // The scene: a triangle at a fixed camera-relative position.
        // We render it at various absolute origins using both correct and
        // naive float conversion, then compare pixel differences.

        const double distances[] = {1e3, 1e6, 1e9, 1e12};

        std::printf("\n=== Naive Float Control: Error vs Distance ===\n");
        std::printf("| Distance (m) | Correct pixels | Naive pixels | Pixel diff | Relative error (m) |\n");
        std::printf("|-------------|----------------|--------------|------------|-------------------|\n");

        for (double dist : distances)
        {
            // Correct path: compose in double, cast to float once.
            auto camPos = WorldPosition::fromMeters(dist, 0.0, 0.0);
            auto objPos = WorldPosition::fromMeters(dist + 100.0, 50.0, -30.0);
            REQUIRE(camPos.has_value());
            REQUIRE(objPos.has_value());

            RenderObject obj;
            obj.position = *objPos;
            obj.localRotationScale = Mat3d(1.0);
            Mat4f correctMvp = obj.cameraRelativeModelMatrix(*camPos);

            // Naive path: round world positions to float32 first, then subtract.
            float naiveCamX = static_cast<float>(dist);
            float naiveObjX = static_cast<float>(dist + 100.0);
            float naiveRelX = naiveObjX - naiveCamX;
            float naiveRelY = static_cast<float>(50.0) - 0.0f;
            float naiveRelZ = static_cast<float>(-30.0) - 0.0f;

            // Build naive MVP: translate by naive relative position.
            Mat4f naiveMvp(1.0f);
            naiveMvp[3][0] = naiveRelX;
            naiveMvp[3][1] = naiveRelY;
            naiveMvp[3][2] = naiveRelZ;

            // Render both.
            auto correctStats = renderTriangle(device->device(), context->physicalDevice(),
                                              device->graphicsQueueFamily(), *allocator,
                                              device->graphicsQueue(), correctMvp);
            auto naiveStats = renderTriangle(device->device(), context->physicalDevice(),
                                            device->graphicsQueueFamily(), *allocator,
                                            device->graphicsQueue(), naiveMvp);

            // Compute relative error in metres.
            float correctRelX = static_cast<float>(100.0);
            float naiveError = std::fabs(naiveRelX - correctRelX);

            std::printf("| %.0e | %u | %u | %u | %.3g |\n",
                        dist, correctStats.nonZeroPixels, naiveStats.nonZeroPixels,
                        (correctStats.nonZeroPixels > naiveStats.nonZeroPixels
                             ? correctStats.nonZeroPixels - naiveStats.nonZeroPixels
                             : naiveStats.nonZeroPixels - correctStats.nonZeroPixels),
                        naiveError);

            // At 1e12 m, naive float should produce a completely wrong result.
            if (dist >= 1e12)
            {
                // The naive relative X should be ~0 (both positions round to same float).
                bool naiveOk = (naiveRelX == 0.0f) || (std::fabs(naiveRelX) < 1.0f);
                CHECK(naiveOk);
            }
        }

        std::printf("=== End Naive Control ===\n\n");

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
