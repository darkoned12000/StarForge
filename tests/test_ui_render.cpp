// M16.2 (T3): built-in bitmap text renders through the internal UI renderer.
#include <doctest/doctest.h>

#include "stb_image.h"
#include "stb_image_write.h"

#include "environment/ui_renderer.hpp"
#include "gameplay/ui_shapes.hpp"
#include "gameplay/ui_text.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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

namespace
{
constexpr std::uint32_t kW = 128;
constexpr std::uint32_t kH = 128;
constexpr VkDeviceSize kPixelBytes = kW * kH * 4;
constexpr int kTolerance = 12;
} // namespace

TEST_CASE("M16.2: bitmap text golden (T3)")
{
    vulkan::ContextCreateInfo info;
    info.enableValidation = true;
    info.requiredExtensions.clear();
    auto context = vulkan::VulkanContext::create(info);
    REQUIRE(context.has_value());
    auto device = vulkan::VulkanDevice::create(*context);
    REQUIRE(device.has_value());
    auto allocator = gpu_resources::Allocator::create({
        .instance = context->instance(), .physicalDevice = context->physicalDevice(),
        .device = device->device()});
    REQUIRE(allocator.has_value());

    auto renderer = environment::UiRenderer::create({
        .instance = context->instance(), .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .shaderDirectory = STARFORGE_SHADER_DIR});
    REQUIRE(renderer.has_value());

    auto color = gpu_resources::Image::create(*allocator, {
        .width = kW, .height = kH, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = gpu_resources::ImageUsageFlags::ColorAttachment |
                 gpu_resources::ImageUsageFlags::TransferSrc});
    REQUIRE(color.has_value());
    auto view = color->createImageView(device->device());
    REQUIRE(view.has_value());
    auto readback = gpu_resources::Buffer::create(*allocator, {
        .size = kPixelBytes, .usage = gpu_resources::BufferUsage::TransferDst,
        .memory = gpu_resources::MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

    // Same path as the public facade: glyph quads -> clip-space vertices.
    std::vector<environment::UiVertex> vertices;
    const auto ndc = [](float px, float py) -> std::array<float, 4> {
        return {2.0f * px / kW - 1.0f, 1.0f - 2.0f * py / kH, 0.0f, 1.0f};
    };
    const std::array<float, 4> colorValue{1.0f, 0.9f, 0.2f, 1.0f};
    for (const auto& q : gameplay::ui_text::textQuads("UI", 16.0f, 16.0f, 4.0f))
    {
        const auto a = ndc(q.x0, q.y0), b = ndc(q.x1, q.y0);
        const auto c = ndc(q.x1, q.y1), d = ndc(q.x0, q.y1);
        vertices.push_back({a, colorValue});
        vertices.push_back({b, colorValue});
        vertices.push_back({c, colorValue});
        vertices.push_back({a, colorValue});
        vertices.push_back({c, colorValue});
        vertices.push_back({d, colorValue});
    }
    REQUIRE_FALSE(vertices.empty());

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

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
    color->transitionToColorAttachment(cmd);
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view->handle();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {kW, kH};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(cmd, &rendering);
    VkViewport viewport{0.0f, static_cast<float>(kH), static_cast<float>(kW),
                        -static_cast<float>(kH), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {kW, kH}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    renderer->render(cmd, vertices);
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
    const auto* pixels = static_cast<const std::uint8_t*>(*mapped);
    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/ui_text_128.png";
    if (std::getenv("STARFORGE_UPDATE_GOLDENS") != nullptr)
    {
        REQUIRE(stbi_write_png(goldenPath.c_str(), kW, kH, 4, pixels, kW * 4) == 1);
    }
    else
    {
        int gw = 0, gh = 0, gc = 0;
        unsigned char* golden = stbi_load(goldenPath.c_str(), &gw, &gh, &gc, 4);
        REQUIRE_MESSAGE(golden != nullptr, "Golden missing: " << goldenPath);
        std::uint32_t failing = 0;
        for (std::size_t i = 0; i < kPixelBytes; ++i)
            if (std::abs(static_cast<int>(pixels[i]) - static_cast<int>(golden[i])) > kTolerance) ++failing;
        stbi_image_free(golden);
        CHECK(static_cast<double>(failing) / kPixelBytes < 0.01);
    }
    std::size_t lit = 0;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
        if (pixels[i] > 100 && pixels[i + 1] > 80) ++lit;
    readback->unmap();
    CHECK(lit > 100);
    CHECK(context->validationErrorCount() == 0);

    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
}

// M16.3 (T3): a composite toon panel (outline, fill, rim, bars, text)
// renders in one draw with the same glyph/geometry path as the facade.
TEST_CASE("M16.3: composite UI panel golden (T3)")
{
    vulkan::ContextCreateInfo info;
    info.enableValidation = true;
    info.requiredExtensions.clear();
    auto context = vulkan::VulkanContext::create(info);
    REQUIRE(context.has_value());
    auto device = vulkan::VulkanDevice::create(*context);
    REQUIRE(device.has_value());
    auto allocator = gpu_resources::Allocator::create({
        .instance = context->instance(), .physicalDevice = context->physicalDevice(),
        .device = device->device()});
    REQUIRE(allocator.has_value());
    auto renderer = environment::UiRenderer::create({
        .instance = context->instance(), .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .shaderDirectory = STARFORGE_SHADER_DIR});
    REQUIRE(renderer.has_value());

    auto color = gpu_resources::Image::create(*allocator, {
        .width = kW, .height = kH, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = gpu_resources::ImageUsageFlags::ColorAttachment |
                 gpu_resources::ImageUsageFlags::TransferSrc});
    REQUIRE(color.has_value());
    auto view = color->createImageView(device->device());
    REQUIRE(view.has_value());
    auto readback = gpu_resources::Buffer::create(*allocator, {
        .size = kPixelBytes, .usage = gpu_resources::BufferUsage::TransferDst,
        .memory = gpu_resources::MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

    struct ColoredRect { float x0,y0,x1,y1; std::array<float,4> color; };
    std::vector<ColoredRect> rects;
    const std::array<float,4> fill{0.08f,0.12f,0.22f,1.0f};
    const std::array<float,4> outline{0.02f,0.02f,0.04f,1.0f};
    const std::array<float,4> accent{0.95f,0.65f,0.15f,1.0f};
    const std::array<float,4> barFill{0.2f,0.8f,0.4f,1.0f};
    const std::array<float,4> textColor{1.0f,0.95f,0.8f,1.0f};
    const std::array<const std::array<float,4>*,3> panelPalette{&fill,&outline,&accent};
    for (const auto& q : gameplay::ui_shapes::panelQuads(8.0f, 8.0f, 112.0f, 48.0f, 2.0f))
        rects.push_back({q.quad.x0,q.quad.y0,q.quad.x1,q.quad.y1,*panelPalette[q.colorIndex]});
    const std::array<const std::array<float,4>*,2> barPalette{&barFill,&outline};
    int bi = 0;
    for (const auto& q : gameplay::ui_shapes::barQuads(16.0f, 36.0f, 96.0f, 4.0f, 0.6f))
        rects.push_back({q.quad.x0,q.quad.y0,q.quad.x1,q.quad.y1,*barPalette[bi++]});
    for (const auto& g : gameplay::ui_text::textQuads("M16", 16.0f, 14.0f, 2.0f))
        rects.push_back({g.x0,g.y0,g.x1,g.y1,textColor});
    // Crosshair + selected-target bracket (M16.5 reticle primitives).
    const std::array<float,4> reticle{1.0f,1.0f,1.0f,0.9f};
    const std::array<float,4> bracket{1.0f,0.3f,0.2f,1.0f};
    const float mx = 64.0f, my = 64.0f;
    rects.push_back({mx-18,my-1,mx-8,my+1,reticle});
    rects.push_back({mx+8,my-1,mx+18,my+1,reticle});
    rects.push_back({mx-1,my-18,mx+1,my-8,reticle});
    rects.push_back({mx-1,my+8,mx+1,my+18,reticle});
    rects.push_back({mx-30,my-30,mx-18,my-28,bracket});
    rects.push_back({mx+18,my-30,mx+30,my-28,bracket});
    rects.push_back({mx-30,my-30,mx-28,my-18,bracket});
    rects.push_back({mx+28,my-30,mx+30,my-18,bracket});
    // Navigation radar rings + contacts (M16.6).
    const std::array<float,4> ring{0.15f,0.35f,0.5f,1.0f};
    const std::array<float,4> hostileDot{1.0f,0.25f,0.2f,1.0f};
    const std::array<float,4> planetDot{0.2f,0.55f,1.0f,1.0f};
    const float rr = 30.0f;
    rects.push_back({mx-rr,my-1,mx+rr,my+1,ring});
    rects.push_back({mx-1,my-rr,mx+1,my+rr,ring});
    for (const auto& c : std::vector<std::pair<float,float>>{{-14.0f,-10.0f},{8.0f,-16.0f},{20.0f,7.0f}})
        rects.push_back({mx+c.first,my+c.second,mx+c.first+3,my+c.second+3,hostileDot});
    rects.push_back({mx-6,my-20,mx-3,my-17,planetDot});
    // Comms log lines + entry prompt (M16.7).
    const std::array<float,4> commsColor{0.6f,0.9f,1.0f,1.0f};
    for (const auto& g : gameplay::ui_text::textQuads("COMMS", 8.0f, 100.0f, 1.0f))
        rects.push_back({g.x0,g.y0,g.x1,g.y1,commsColor});
    for (const auto& g : gameplay::ui_text::textQuads(">hi", 8.0f, 112.0f, 1.0f))
        rects.push_back({g.x0,g.y0,g.x1,g.y1,reticle});
    // Station buttons (M16.8): small outlined panels with labels.
    const std::array<float,4> buttonFill{0.1f,0.2f,0.32f,1.0f};
    const std::array<const std::array<float,4>*,3> buttonPalette{&buttonFill,&outline,&accent};
    int bpi = 0;
    for (const auto& q : gameplay::ui_shapes::panelQuads(64.0f, 96.0f, 56.0f, 14.0f, 1.0f))
        rects.push_back({q.quad.x0,q.quad.y0,q.quad.x1,q.quad.y1,*buttonPalette[q.colorIndex]});
    for (const auto& g : gameplay::ui_text::textQuads("BUY", 68.0f, 99.0f, 1.0f))
        rects.push_back({g.x0,g.y0,g.x1,g.y1,textColor});
    // Toast banner (M16.9).
    const std::array<float,4> toastFill{0.25f,0.1f,0.06f,1.0f};
    const std::array<const std::array<float,4>*,3> toastPalette{&toastFill,&outline,&accent};
    for (const auto& q : gameplay::ui_shapes::panelQuads(34.0f, 84.0f, 60.0f, 12.0f, 1.0f))
        rects.push_back({q.quad.x0,q.quad.y0,q.quad.x1,q.quad.y1,*toastPalette[q.colorIndex]});

    std::vector<environment::UiVertex> vertices;
    const auto ndc = [](float px, float py) -> std::array<float,4> {
        return {2.0f*px/kW - 1.0f, 1.0f - 2.0f*py/kH, 0.0f, 1.0f};
    };
    for (const auto& r : rects)
    {
        const auto a=ndc(r.x0,r.y0), b=ndc(r.x1,r.y0), c=ndc(r.x1,r.y1), d=ndc(r.x0,r.y1);
        vertices.push_back({a,r.color}); vertices.push_back({b,r.color});
        vertices.push_back({c,r.color}); vertices.push_back({a,r.color});
        vertices.push_back({c,r.color}); vertices.push_back({d,r.color});
    }
    REQUIRE_FALSE(vertices.empty());

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

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
    color->transitionToColorAttachment(cmd);
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view->handle();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = VkClearColorValue{{0.0f,0.0f,0.0f,1.0f}};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {kW,kH};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(cmd, &rendering);
    VkViewport viewport{0.0f,static_cast<float>(kH),static_cast<float>(kW),-static_cast<float>(kH),0.0f,1.0f};
    VkRect2D scissor{{0,0},{kW,kH}};
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&scissor);
    renderer->render(cmd, vertices);
    vkCmdEndRendering(cmd);
    color->transitionToTransferSrc(cmd);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {kW,kH,1};
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
    REQUIRE(device->submit(1,&submit,fence) == VK_SUCCESS);
    REQUIRE(vkWaitForFences(device->device(),1,&fence,VK_TRUE,UINT64_MAX) == VK_SUCCESS);

    auto mapped = readback->map();
    REQUIRE(mapped.has_value());
    (void)readback->invalidate(0,kPixelBytes);
    const auto* pixels = static_cast<const std::uint8_t*>(*mapped);
    const std::string path = std::string(STARFORGE_GOLDEN_DIR) + "/ui_panel_128.png";
    if (std::getenv("STARFORGE_UPDATE_GOLDENS") != nullptr)
        REQUIRE(stbi_write_png(path.c_str(), kW, kH, 4, pixels, kW*4) == 1);
    else
    {
        int gw=0,gh=0,gc=0;
        unsigned char* golden = stbi_load(path.c_str(),&gw,&gh,&gc,4);
        REQUIRE_MESSAGE(golden != nullptr, "Golden missing: " << path);
        std::uint32_t failing=0;
        for (std::size_t i=0;i<kPixelBytes;++i)
            if (std::abs(int(pixels[i])-int(golden[i]))>kTolerance) ++failing;
        stbi_image_free(golden);
        CHECK(double(failing)/kPixelBytes < 0.01);
    }
    std::size_t lit=0;
    for (std::size_t i=0;i<kPixelBytes;i+=4)
        if (pixels[i]+pixels[i+1]+pixels[i+2] > 40) ++lit;
    readback->unmap();
    CHECK(lit > 500);
    CHECK(context->validationErrorCount() == 0);
    vkDestroyFence(device->device(),fence,nullptr);
    vkFreeCommandBuffers(device->device(),pool,1,&cmd);
    vkDestroyCommandPool(device->device(),pool,nullptr);
}

// Regression: a busy HUD must not silently vanish when glyph quads exceed
// the initial vertex buffer capacity (the old 256 KB cap dropped the UI).
TEST_CASE("M16.9: large text draw survives buffer growth")
{
    vulkan::ContextCreateInfo info;
    info.enableValidation = true;
    info.requiredExtensions.clear();
    auto context = vulkan::VulkanContext::create(info);
    REQUIRE(context.has_value());
    auto device = vulkan::VulkanDevice::create(*context);
    REQUIRE(device.has_value());
    auto allocator = gpu_resources::Allocator::create({
        .instance = context->instance(), .physicalDevice = context->physicalDevice(),
        .device = device->device()});
    REQUIRE(allocator.has_value());
    auto renderer = environment::UiRenderer::create({
        .instance = context->instance(), .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .shaderDirectory = STARFORGE_SHADER_DIR});
    REQUIRE(renderer.has_value());
    auto image = gpu_resources::Image::create(*allocator, {
        .width = kW, .height = kH, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = gpu_resources::ImageUsageFlags::ColorAttachment |
                 gpu_resources::ImageUsageFlags::TransferSrc});
    REQUIRE(image.has_value());
    auto view = image->createImageView(device->device());
    REQUIRE(view.has_value());
    auto readback = gpu_resources::Buffer::create(*allocator, {
        .size = kPixelBytes, .usage = gpu_resources::BufferUsage::TransferDst,
        .memory = gpu_resources::MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

    // 18 rows x 20 glyphs x ~20 lit pixels = ~7200 quads > the old cap.
    const std::string row(20, 'M');
    std::vector<gameplay::ui_text::Quad> quads;
    for (int r = 0; r < 18; ++r)
    {
        auto rowQuads = gameplay::ui_text::textQuads(row, 2.0f, 2.0f + r * 7.0f, 1.0f);
        quads.insert(quads.end(), rowQuads.begin(), rowQuads.end());
    }
    REQUIRE(quads.size() > 1600);
    std::vector<environment::UiVertex> vertices;
    const auto ndc = [](float px, float py) -> std::array<float,4> {
        return {2.0f*px/kW-1.0f, 1.0f-2.0f*py/kH, 0.0f, 1.0f};
    };
    const std::array<float,4> color{1.0f,1.0f,0.2f,1.0f};
    for (const auto& q : quads)
    {
        const auto a=ndc(q.x0,q.y0), b=ndc(q.x1,q.y0), c=ndc(q.x1,q.y1), d=ndc(q.x0,q.y1);
        vertices.push_back({a,color}); vertices.push_back({b,color});
        vertices.push_back({c,color}); vertices.push_back({a,color});
        vertices.push_back({c,color}); vertices.push_back({d,color});
    }

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

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
    image->transitionToColorAttachment(cmd);
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view->handle();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = VkClearColorValue{{0.0f,0.0f,0.0f,1.0f}};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {kW,kH};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(cmd, &rendering);
    VkViewport viewport{0.0f,static_cast<float>(kH),static_cast<float>(kW),-static_cast<float>(kH),0.0f,1.0f};
    VkRect2D scissor{{0,0},{kW,kH}};
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&scissor);
    renderer->render(cmd, vertices);
    vkCmdEndRendering(cmd);
    image->transitionToTransferSrc(cmd);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {kW,kH,1};
    vkCmdCopyImageToBuffer(cmd, image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
    REQUIRE(device->submit(1,&submit,fence) == VK_SUCCESS);
    REQUIRE(vkWaitForFences(device->device(),1,&fence,VK_TRUE,UINT64_MAX) == VK_SUCCESS);

    auto mapped = readback->map();
    REQUIRE(mapped.has_value());
    (void)readback->invalidate(0,kPixelBytes);
    const auto* pixels = static_cast<const std::uint8_t*>(*mapped);
    std::size_t lit = 0;
    for (std::size_t i=0;i<kPixelBytes;i+=4)
        if (pixels[i] > 100 && pixels[i+1] > 100) ++lit;
    readback->unmap();
    CHECK(lit > 400); // the whole HUD still renders, not an empty frame

    // T4: repeated busy-HUD frames stay under the ADR-004 interval.
    std::vector<double> timings;
    for (int frame = 0; frame < 14; ++frame)
    {
        REQUIRE(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
        VkCommandBufferBeginInfo loopBegin{};
        loopBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        loopBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        REQUIRE(vkBeginCommandBuffer(cmd, &loopBegin) == VK_SUCCESS);
        image->transitionToColorAttachment(cmd);
        VkRenderingAttachmentInfo loopAttachment{};
        loopAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        loopAttachment.imageView = view->handle();
        loopAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        loopAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        loopAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        loopAttachment.clearValue.color = VkClearColorValue{{0.0f,0.0f,0.0f,1.0f}};
        VkRenderingInfo loopRendering{};
        loopRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        loopRendering.renderArea.extent = {kW,kH};
        loopRendering.layerCount = 1;
        loopRendering.colorAttachmentCount = 1;
        loopRendering.pColorAttachments = &loopAttachment;
        vkCmdBeginRendering(cmd, &loopRendering);
        VkViewport vp{0.0f,static_cast<float>(kH),static_cast<float>(kW),-static_cast<float>(kH),0.0f,1.0f};
        VkRect2D sc{{0,0},{kW,kH}};
        vkCmdSetViewport(cmd,0,1,&vp);
        vkCmdSetScissor(cmd,0,1,&sc);
        renderer->render(cmd, vertices);
        vkCmdEndRendering(cmd);
        REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);
        REQUIRE(vkResetFences(device->device(),1,&fence) == VK_SUCCESS);
        const auto started = std::chrono::steady_clock::now();
        REQUIRE(device->submit(1,&submit,fence) == VK_SUCCESS);
        REQUIRE(vkWaitForFences(device->device(),1,&fence,VK_TRUE,UINT64_MAX) == VK_SUCCESS);
        const auto ended = std::chrono::steady_clock::now();
        if (frame >= 2)
            timings.push_back(std::chrono::duration<double,std::milli>(ended-started).count());
    }
    std::sort(timings.begin(), timings.end());
    CHECK(timings[(timings.size()*95)/100] < 6.94);
    CHECK(context->validationErrorCount() == 0);
    vkDestroyFence(device->device(),fence,nullptr);
    vkFreeCommandBuffers(device->device(),pool,1,&cmd);
    vkDestroyCommandPool(device->device(),pool,nullptr);
}
