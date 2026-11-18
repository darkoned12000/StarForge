// M17 / toon integration: GLB decoding (UVs, tangents, embedded texture) and
// a headless textured-toon render proof.
#include <doctest/doctest.h>

#include "asset/gltf_loader.hpp"
#include "environment/toon_renderer.hpp"
#include "math/math.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::asset;
using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;
namespace math = starforge::math;

namespace
{

std::uint32_t u32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}
void append(std::vector<std::uint8_t>& out, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

std::vector<std::uint8_t> makeTexturedQuadGlb()
{
    // 2x2 texture: red, green / blue, white.
    std::vector<std::uint8_t> pixels;
    for (std::uint32_t y = 0; y < 2; ++y)
        for (std::uint32_t x = 0; x < 2; ++x)
        {
            const std::uint8_t color[4] = {
                static_cast<std::uint8_t>(x == 0 ? 255 : 0),
                static_cast<std::uint8_t>(y == 0 ? 255 : 0), 255, 255};
            pixels.insert(pixels.end(), color, color + 4);
        }
    // A minimal 1x1 opaque PNG embedded in the GLB binary chunk.
    static const std::uint8_t kPng[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,
        0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1F,
        0x15,0xC4,0x89,0x00,0x00,0x00,0x0D,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0xF8,
        0xEF,0xA0,0xF0,0x1F,0x00,0x06,0x00,0x02,0x5F,0x34,0xCD,0x40,0xC0,0x00,0x00,
        0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82};
    const std::uint8_t* imageBytes = kPng;
    const std::size_t imageSize = sizeof(kPng);

    const float positions[] = {-0.8f,-0.8f,0.0f, 0.8f,-0.8f,0.0f, 0.8f,0.8f,0.0f, -0.8f,0.8f,0.0f};
    const float normals[] = {0,0,1, 0,0,1, 0,0,1, 0,0,1};
    const float uvs[] = {0,0, 1,0, 1,1, 0,1};
    const float tangents[] = {1,0,0,1, 1,0,0,1, 1,0,0,1, 1,0,0,1};
    const std::uint16_t indices[] = {0,1,2, 0,2,3};

    std::vector<std::uint8_t> bin;
    auto bufferView = [&](const void* data, std::size_t size) {
        const std::uint32_t offset = static_cast<std::uint32_t>(bin.size());
        append(bin, data, size);
        while (bin.size() % 4 != 0) bin.push_back(0);
        return offset;
    };
    const std::uint32_t pPos = bufferView(positions, sizeof(positions));
    const std::uint32_t pNorm = bufferView(normals, sizeof(normals));
    const std::uint32_t pUv = bufferView(uvs, sizeof(uvs));
    const std::uint32_t pTan = bufferView(tangents, sizeof(tangents));
    const std::uint32_t pIdx = bufferView(indices, sizeof(indices));
    const std::uint32_t pImg = bufferView(imageBytes, imageSize);

    nlohmann::json j;
    j["asset"] = {{"version", "2.0"}};
    j["bufferViews"] = {
        {{"buffer",0},{"byteOffset",pPos},{"byteLength",sizeof(positions)}},
        {{"buffer",0},{"byteOffset",pNorm},{"byteLength",sizeof(normals)}},
        {{"buffer",0},{"byteOffset",pUv},{"byteLength",sizeof(uvs)}},
        {{"buffer",0},{"byteOffset",pTan},{"byteLength",sizeof(tangents)}},
        {{"buffer",0},{"byteOffset",pIdx},{"byteLength",sizeof(indices)}},
        {{"buffer",0},{"byteOffset",pImg},{"byteLength",imageSize}},
    };
    j["accessors"] = {
        {{"bufferView",0},{"componentType",5126},{"count",4},{"type","VEC3"}},
        {{"bufferView",1},{"componentType",5126},{"count",4},{"type","VEC3"}},
        {{"bufferView",2},{"componentType",5126},{"count",4},{"type","VEC2"}},
        {{"bufferView",3},{"componentType",5126},{"count",4},{"type","VEC4"}},
        {{"bufferView",4},{"componentType",5123},{"count",6},{"type","SCALAR"}},
    };
    j["images"] = {{{"bufferView",5},{"mimeType","image/png"}}};
    j["textures"] = {{{"source",0}}};
    j["materials"] = {{{"name","ToonBase"},
        {"pbrMetallicRoughness",{{"baseColorFactor",{1.0,0.8,0.6,1.0}},
                                  {"baseColorTexture",{{"index",0}}}}}}};
    j["meshes"] = {{{"name","TexturedQuad"},{"primitives",{{
        {"attributes",{{"POSITION",0},{"NORMAL",1},{"TEXCOORD_0",2},{"TANGENT",3}}},
        {"indices",4},{"material",0}}}}}};
    j["nodes"] = {{{"mesh",0}}};
    j["scenes"] = {{{"nodes",{0}}}};
    j["scene"] = 0;
    j["buffers"] = {{{"byteLength",bin.size()}}};

    std::string jsonText = j.dump();
    while (jsonText.size() % 4 != 0) jsonText.push_back(' ');

    std::vector<std::uint8_t> glb;
    put32(glb, 0x46546C67u);
    put32(glb, 2);
    put32(glb, static_cast<std::uint32_t>(12 + 8 + jsonText.size() + 8 + bin.size()));
    put32(glb, static_cast<std::uint32_t>(jsonText.size()));
    put32(glb, 0x4E4F534Au);
    append(glb, jsonText.data(), jsonText.size());
    put32(glb, static_cast<std::uint32_t>(bin.size()));
    put32(glb, 0x004E4942u);
    append(glb, bin.data(), bin.size());
    return glb;
}

} // namespace

TEST_CASE("Asset: GLB decode exposes UVs, tangents and material texture")
{
    const auto glb = makeTexturedQuadGlb();
    auto asset = loadGlb(glb.data(), glb.size());
    REQUIRE(asset.has_value());
    REQUIRE(asset->meshes.size() == 1);
    const auto& mesh = asset->meshes[0];
    CHECK(mesh.name == "TexturedQuad");
    CHECK(mesh.vertices.size() == 6); // indices expanded
    CHECK(mesh.indices.size() == 6);
    CHECK(mesh.material.name == "ToonBase");
    CHECK(mesh.material.baseColorFactor[0] == doctest::Approx(1.0f));
    CHECK(mesh.material.hasBaseColorTexture);
    CHECK(mesh.material.baseColorTexture.width == 1);
    CHECK(mesh.material.baseColorTexture.height == 1);
    CHECK(mesh.material.baseColorTexture.rgba.size() == 4);
    CHECK(mesh.vertices[0].uv[0] == doctest::Approx(0.0f));
    CHECK(mesh.vertices[1].uv[0] == doctest::Approx(1.0f));
    CHECK(mesh.vertices[0].tangent[0] == doctest::Approx(1.0f));
}

TEST_CASE("Asset: malformed GLB is rejected")
{
    const std::uint8_t bad[] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_FALSE(loadGlb(bad, sizeof(bad)).has_value());
}

#ifdef STARFORGE_GPU_TESTS
TEST_CASE("Asset: textured glTF mesh renders through the toon material (T3)")
{
    ContextCreateInfo contextInfo;
    contextInfo.enableValidation = true;
    contextInfo.requiredExtensions.clear();
    auto context = VulkanContext::create(contextInfo);
    REQUIRE(context.has_value());
    auto device = VulkanDevice::create(*context);
    REQUIRE(device.has_value());
    auto allocator = Allocator::create({
        .instance = context->instance(), .physicalDevice = context->physicalDevice(),
        .device = device->device()});
    REQUIRE(allocator.has_value());

    constexpr std::uint32_t w = 64, h = 64;
    auto image = Image::create(*allocator, {
        .width = w, .height = h, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc});
    REQUIRE(image.has_value());
    auto view = image->createImageView(device->device());
    REQUIRE(view.has_value());
    auto readback = Buffer::create(*allocator, {
        .size = w * h * 4, .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu});
    REQUIRE(readback.has_value());

    auto toon = ToonRenderer::create({
        .instance = context->instance(), .device = device->device(),
        .physicalDevice = context->physicalDevice(), .queueFamily = device->graphicsQueueFamily(),
        .framesInFlight = 1, .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = VK_FORMAT_UNDEFINED, // offscreen pass has no depth attachment
        .shaderDirectory = STARFORGE_SHADER_DIR});
    REQUIRE(toon.has_value());

    const auto glb = makeTexturedQuadGlb();
    auto asset = loadGlb(glb.data(), glb.size());
    REQUIRE(asset.has_value());
    const auto& mesh = asset->meshes[0];
    std::vector<ToonTexturedVertex> vertices;
    for (const auto& v : mesh.vertices)
    {
        vertices.push_back({
            math::Vec3f{v.position[0], v.position[1], v.position[2]},
            math::Vec3f{v.normal[0], v.normal[1], v.normal[2]},
            math::Vec3f{v.normal[0], v.normal[1], v.normal[2]},
            math::Vec2f{v.uv[0], v.uv[1]},
            math::Vec4f{v.tangent[0], v.tangent[1], v.tangent[2], v.tangent[3]}});
    }
    auto gpuMesh = toon->createTexturedMesh(vertices, mesh.material.baseColorTexture.rgba.data(),
                                            mesh.material.baseColorTexture.width,
                                            mesh.material.baseColorTexture.height);
    REQUIRE(gpuMesh.has_value());

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
    attachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {w, h};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(cmd, &rendering);
    VkViewport viewport{0.0f, static_cast<float>(h), static_cast<float>(w), -static_cast<float>(h), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {w, h}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    ToonRenderContext ctx{};
    ctx.viewProj = math::perspectiveVulkan(glm::radians(60.0f), 1.0f, 0.1f, 10.0f) *
                   glm::lookAt(math::Vec3f{0, 0, 2}, math::Vec3f{0, 0, 0}, math::Vec3f{0, 1, 0});
    ctx.modelView = math::Mat4f{1.0f};
    ctx.resolution = {static_cast<float>(w), static_cast<float>(h)};
    ToonParams params{};
    params.capabilities = 32u; // texture sampling
    params.baseColor = math::Vec4f{mesh.material.baseColorFactor[0], mesh.material.baseColorFactor[1],
                                   mesh.material.baseColorFactor[2], 1.0f};
    params.outlineWidth = 0.0f;
    // Disable depth (the offscreen target has no depth attachment).
    toon->renderMesh(cmd, ctx, params, *gpuMesh);
    vkCmdEndRendering(cmd);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {w, h, 1};
    image->transitionToTransferSrc(cmd);
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

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);
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
    (void)readback->invalidate(0, w * h * 4);
    const auto* pixels = static_cast<const std::uint8_t*>(*mapped);
    std::size_t lit = 0, reddish = 0;
    for (std::size_t i = 0; i < w * h * 4; i += 4)
    {
        if (pixels[i] + pixels[i + 1] + pixels[i + 2] > 30) ++lit;
        if (pixels[i] > pixels[i + 1] + 20) ++reddish;
    }
    readback->unmap();
    CHECK(lit > 100);     // the glTF mesh is visible
    CHECK(reddish > 50);  // embedded texture tints it red
    CHECK(context->validationErrorCount() == 0);

    vkDestroyFence(device->device(), fence, nullptr);
    vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
    vkDestroyCommandPool(device->device(), pool, nullptr);
}
#endif // STARFORGE_GPU_TESTS
