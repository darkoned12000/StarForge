// Golden + budget tests for the cel-shaded proof scene (M9.5.4):
// starfield background with a toon-shaded ship stand-in, rendered in
// one dynamic-rendering pass through the internal renderers.
#include <doctest/doctest.h>

// stb implementations live in test_golden_starfield.cpp (same binary).
#include "stb_image.h"
#include "stb_image_write.h"

#include "environment/starfield_renderer.hpp"
#include "environment/toon_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

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

constexpr std::uint32_t kW = 128;
constexpr std::uint32_t kH = 128;
constexpr int kPerChannelTolerance = 8;

struct Scene
{
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<Allocator> allocator;
    std::unique_ptr<Image> colorImage;
    std::unique_ptr<Image> depthImage;
    std::unique_ptr<Buffer> readback;
    std::optional<StarfieldRenderer> starRenderer;
    std::optional<ToonRenderer> toonRenderer;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};

// Records one composed frame (starfield background, then toon cube).
void recordScene(Scene& scene)
{
    vkResetCommandBuffer(scene.cmd, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(scene.cmd, &beginInfo);

    Image::transitionToColorAttachment(scene.cmd, scene.colorImage->handle());
    Image::transitionLayout(scene.cmd, scene.depthImage->handle(),
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = scene.colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = scene.depthView;
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

    vkCmdBeginRendering(scene.cmd, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(kH);
    viewport.width = static_cast<float>(kW);
    viewport.height = -static_cast<float>(kH);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(scene.cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {kW, kH};
    vkCmdSetScissor(scene.cmd, 0, 1, &scissor);

    const auto view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);

    StarfieldRenderContext starContext;
    starContext.viewProj = projection * view;
    starContext.invViewProj = glm::inverse(starContext.viewProj);
    starContext.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
    starContext.time = 0.016f;
    StarfieldParams starParams;
    starParams.worldPosition = {-15.0f, 8.0f, -40.0f};
    starParams.radius = 20.0f;
    starParams.time = 0.016f;
    scene.starRenderer->render(scene.cmd, starContext, starParams);

    ToonRenderContext toonContext;
    toonContext.viewProj = projection * view;
    toonContext.modelView = view;
    toonContext.lightDirectionView = {0.4f, 0.5f, 0.75f};
    toonContext.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
    ToonParams toonParams;
    toonParams.baseColor = {0.55f, 0.6f, 0.7f, 1.0f};
    toonParams.shadowColor = {0.15f, 0.15f, 0.35f, 1.0f};
    toonParams.bandCount = 3.0f;
    toonParams.rimStrength = 0.6f;
    toonParams.outlineWidth = 2.0f;
    scene.toonRenderer->render(scene.cmd, toonContext, toonParams);

    vkCmdEndRendering(scene.cmd);

    Image::transitionLayout(scene.cmd, scene.colorImage->handle(),
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
    vkCmdCopyImageToBuffer(scene.cmd, scene.colorImage->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           scene.readback->handle(), 1, &region);

    VkBufferMemoryBarrier2 hostBarrier{};
    hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = scene.readback->handle();
    hostBarrier.offset = 0;
    hostBarrier.size = VK_WHOLE_SIZE;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &hostBarrier;
    vkCmdPipelineBarrier2(scene.cmd, &dep);

    vkEndCommandBuffer(scene.cmd);
}

void submitAndWait(Scene& scene)
{
    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = scene.cmd;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;
    vkQueueSubmit2(scene.device->graphicsQueue(), 1, &submitInfo, scene.fence);
    vkWaitForFences(scene.device->device(), 1, &scene.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(scene.device->device(), 1, &scene.fence);
}

[[nodiscard]] Scene makeScene()
{
    Scene scene;

    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();
    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());
    scene.context = std::make_unique<VulkanContext>(std::move(*context));

    auto device = VulkanDevice::create(*scene.context);
    REQUIRE(device.has_value());
    scene.device = std::make_unique<VulkanDevice>(std::move(*device));

    auto allocator = Allocator::create({
        .instance = scene.context->instance(),
        .physicalDevice = scene.context->physicalDevice(),
        .device = scene.device->device(),
    });
    REQUIRE(allocator.has_value());
    scene.allocator = std::make_unique<Allocator>(std::move(*allocator));

    auto colorImage = Image::create(*scene.allocator, {
        .width = kW,
        .height = kH,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsageFlags::ColorAttachment,
    });
    REQUIRE(colorImage.has_value());
    scene.colorImage = std::make_unique<Image>(std::move(*colorImage));
    auto colorView = scene.colorImage->createView(scene.device->device());
    REQUIRE(colorView.has_value());
    scene.colorView = *colorView;

    const VkFormat depthFormat = findDepthFormat(scene.context->physicalDevice());
    REQUIRE(depthFormat != VK_FORMAT_UNDEFINED);
    auto depthImage = Image::create(*scene.allocator, {
        .width = kW,
        .height = kH,
        .format = depthFormat,
        .usage = ImageUsageFlags::DepthAttachment,
    });
    REQUIRE(depthImage.has_value());
    scene.depthImage = std::make_unique<Image>(std::move(*depthImage));
    auto depthView = scene.depthImage->createView(scene.device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
    REQUIRE(depthView.has_value());
    scene.depthView = *depthView;

    auto readback = Buffer::create(*scene.allocator, {
        .size = kW * kH * 4,
        .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu,
    });
    REQUIRE(readback.has_value());
    scene.readback = std::make_unique<Buffer>(std::move(*readback));

    auto starRenderer = StarfieldRenderer::create({
        .device = scene.device->device(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = depthFormat,
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    REQUIRE(starRenderer.has_value());
    scene.starRenderer = std::move(*starRenderer);

    auto toonRenderer = ToonRenderer::create({
        .device = scene.device->device(),
        .physicalDevice = scene.context->physicalDevice(),
        .queueFamily = scene.device->graphicsQueueFamily(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = depthFormat,
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    REQUIRE(toonRenderer.has_value());
    scene.toonRenderer = std::move(*toonRenderer);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = scene.device->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    REQUIRE(vkCreateCommandPool(scene.device->device(), &poolInfo, nullptr, &scene.cmdPool) == VK_SUCCESS);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = scene.cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    REQUIRE(vkAllocateCommandBuffers(scene.device->device(), &allocInfo, &scene.cmd) == VK_SUCCESS);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    REQUIRE(vkCreateFence(scene.device->device(), &fenceInfo, nullptr, &scene.fence) == VK_SUCCESS);

    return scene;
}

void destroyScene(Scene& scene)
{
    scene.toonRenderer.reset();
    scene.starRenderer.reset();
    vkDestroyFence(scene.device->device(), scene.fence, nullptr);
    vkDestroyImageView(scene.device->device(), scene.colorView, nullptr);
    vkDestroyImageView(scene.device->device(), scene.depthView, nullptr);
    vkFreeCommandBuffers(scene.device->device(), scene.cmdPool, 1, &scene.cmd);
    vkDestroyCommandPool(scene.device->device(), scene.cmdPool, nullptr);
    scene.readback.reset();
    scene.depthImage.reset();
    scene.colorImage.reset();
    scene.allocator.reset();
    scene.device.reset();
    scene.context.reset();
}

} // namespace

TEST_CASE("Golden: cel-shaded proof scene matches reference image (9.5.4)")
{
    auto scene = makeScene();
    recordScene(scene);
    submitAndWait(scene);

    auto mapped = scene.readback->map();
    REQUIRE(mapped.has_value());
    (void)scene.readback->invalidate(0, kW * kH * 4);
    const auto* actual = static_cast<const std::uint8_t*>(*mapped);

    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/toon_scene_128.png";
    const bool updateMode = std::getenv("STARFORGE_UPDATE_GOLDENS") != nullptr;

    if (updateMode)
    {
        REQUIRE(stbi_write_png(goldenPath.c_str(), static_cast<int>(kW), static_cast<int>(kH), 4, actual,
                               static_cast<int>(kW * 4)) == 1);
    }
    else
    {
        int gw = 0, gh = 0, gcomp = 0;
        unsigned char* golden = stbi_load(goldenPath.c_str(), &gw, &gh, &gcomp, 4);
        REQUIRE_MESSAGE(golden != nullptr, "Golden file missing: " << goldenPath);
        REQUIRE(gw == static_cast<int>(kW));
        REQUIRE(gh == static_cast<int>(kH));

        std::uint32_t failing = 0;
        for (std::uint32_t i = 0; i < kW * kH * 4; ++i)
        {
            int diff = static_cast<int>(actual[i]) - static_cast<int>(golden[i]);
            if (std::abs(diff) > kPerChannelTolerance)
            {
                ++failing;
            }
        }
        stbi_image_free(golden);
        CHECK(static_cast<double>(failing) / static_cast<double>(kW * kH * 4) < 0.01);
    }
    scene.readback->unmap();

    destroyScene(scene);
}

TEST_CASE("Budget: cel-shaded scene frame time under ADR-004 interval (9.5.4)")
{
    auto scene = makeScene();

    constexpr int kFrames = 30;
    std::vector<double> timings;
    timings.reserve(kFrames);
    for (int frame = 0; frame < kFrames; ++frame)
    {
        recordScene(scene);
        const auto frameStart = std::chrono::steady_clock::now();
        submitAndWait(scene);
        const auto frameEnd = std::chrono::steady_clock::now();
        if (frame >= 3)
        {
            timings.push_back(std::chrono::duration<double, std::milli>(frameEnd - frameStart).count());
        }
    }

    std::sort(timings.begin(), timings.end());
    const double p95 = timings[(timings.size() * 95) / 100];
    CHECK(p95 < 6.94);

    destroyScene(scene);
}
