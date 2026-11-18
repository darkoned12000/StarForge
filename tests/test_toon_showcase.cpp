// M9.5.6 production toon-showcase acceptance: a composed scene (ship hull,
// glowing exhaust, nebula, planet with atmosphere rim, outlined asteroids and
// a weapon fire effect) rendered headlessly with golden + budget gates.
#include <doctest/doctest.h>

#include "stb_image.h"
#include "stb_image_write.h"

#include "environment/asteroid_field.hpp"
#include "environment/nebula_renderer.hpp"
#include "environment/starfield_renderer.hpp"
#include "environment/toon_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
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
constexpr VkDeviceSize kPixelBytes = kW * kH * 4;
constexpr int kTolerance = 10;
constexpr float kPi = 3.14159265f;

math::Vec3f v3(float x, float y, float z) { return math::Vec3f{x, y, z}; }

void pushTri(std::vector<ToonVertex>& out,
             math::Vec3f a, math::Vec3f b, math::Vec3f c, math::Vec3f n)
{
    const math::Vec3f oa = glm::normalize(a);
    const math::Vec3f ob = glm::normalize(b);
    const math::Vec3f oc = glm::normalize(c);
    out.push_back({a, n, oa});
    out.push_back({b, n, ob});
    out.push_back({c, n, oc});
}

// Chamfered hull: an elongated hexagonal prism with tapered nose and two
// wings — deliberately not a cube, so the silhouette reads as a ship.
std::vector<ToonVertex> makeShipHull()
{
    std::vector<ToonVertex> out;
    const float front = 1.4f, back = -1.0f, w = 0.45f, h = 0.22f;
    const math::Vec3f top{0.0f, h, 0.0f};
    const math::Vec3f bottom{0.0f, -h, 0.0f};
    const math::Vec3f nose{0.0f, 0.0f, front};
    const math::Vec3f lf{-w, 0.0f, 0.3f}, rf{w, 0.0f, 0.3f};
    const math::Vec3f lb{-w, 0.0f, back}, rb{w, 0.0f, back};

    pushTri(out, nose, lf, top, glm::normalize(v3(-0.5f, 0.5f, 0.7f)));
    pushTri(out, nose, top, rf, glm::normalize(v3(0.5f, 0.5f, 0.7f)));
    pushTri(out, nose, bottom, lf, glm::normalize(v3(-0.5f, -0.5f, 0.7f)));
    pushTri(out, nose, rf, bottom, glm::normalize(v3(0.5f, -0.5f, 0.7f)));
    pushTri(out, top, lf, lb, glm::normalize(v3(-0.7f, 0.5f, 0.0f)));
    pushTri(out, top, lb, rb, v3(0.0f, 1.0f, 0.0f));
    pushTri(out, top, rb, rf, glm::normalize(v3(0.7f, 0.5f, 0.0f)));
    pushTri(out, bottom, lb, lf, glm::normalize(v3(-0.7f, -0.5f, 0.0f)));
    pushTri(out, bottom, rb, lb, v3(0.0f, -1.0f, 0.0f));
    pushTri(out, bottom, rf, rb, glm::normalize(v3(0.7f, -0.5f, 0.0f)));
    pushTri(out, lf, lb, rb, v3(0.0f, 0.0f, -1.0f));
    pushTri(out, lf, rb, rf, v3(0.0f, 0.0f, -1.0f));

    // Wings (flat plates with two-sided normals).
    const math::Vec3f wl0{-0.35f, 0.0f, 0.2f}, wl1{-1.3f, 0.0f, -0.6f};
    const math::Vec3f wl2{-0.35f, 0.0f, -0.7f};
    pushTri(out, wl0, wl1, wl2, v3(0.0f, 1.0f, 0.0f));
    pushTri(out, wl2, wl1, wl0, v3(0.0f, -1.0f, 0.0f));
    const math::Vec3f wr0{0.35f, 0.0f, 0.2f}, wr1{1.3f, 0.0f, -0.6f};
    const math::Vec3f wr2{0.35f, 0.0f, -0.7f};
    pushTri(out, wr0, wr2, wr1, v3(0.0f, 1.0f, 0.0f));
    pushTri(out, wr1, wr2, wr0, v3(0.0f, -1.0f, 0.0f));
    return out;
}

std::vector<ToonVertex> makeCone(float radius, float length, std::uint32_t segments)
{
    std::vector<ToonVertex> out;
    for (std::uint32_t i = 0; i < segments; ++i)
    {
        const float a0 = 2.0f * kPi * i / segments;
        const float a1 = 2.0f * kPi * (i + 1) / segments;
        const math::Vec3f p0{std::cos(a0) * radius, std::sin(a0) * radius, 0.0f};
        const math::Vec3f p1{std::cos(a1) * radius, std::sin(a1) * radius, 0.0f};
        const math::Vec3f tip{0.0f, 0.0f, length};
        const math::Vec3f n = glm::normalize(v3(std::cos(a0), std::sin(a0), 0.2f));
        pushTri(out, p0, tip, p1, n);
    }
    return out;
}

std::vector<ToonVertex> makeSphere(float radius, std::uint32_t lat, std::uint32_t lon)
{
    std::vector<ToonVertex> out;
    for (std::uint32_t y = 0; y < lat; ++y)
    {
        const float t0 = kPi * y / lat;
        const float t1 = kPi * (y + 1) / lat;
        for (std::uint32_t x = 0; x < lon; ++x)
        {
            const float p0 = 2.0f * kPi * x / lon;
            const float p1 = 2.0f * kPi * (x + 1) / lon;
            auto point = [&](float t, float p) {
                return v3(std::sin(t) * std::cos(p), std::cos(t), std::sin(t) * std::sin(p)) * radius;
            };
            const math::Vec3f a = point(t0, p0), b = point(t1, p0);
            const math::Vec3f c = point(t1, p1), d = point(t0, p1);
            out.push_back({a, glm::normalize(a), glm::normalize(a)});
            out.push_back({b, glm::normalize(b), glm::normalize(b)});
            out.push_back({c, glm::normalize(c), glm::normalize(c)});
            out.push_back({a, glm::normalize(a), glm::normalize(a)});
            out.push_back({c, glm::normalize(c), glm::normalize(c)});
            out.push_back({d, glm::normalize(d), glm::normalize(d)});
        }
    }
    return out;
}

std::vector<ToonVertex> makeAsteroid(float radius)
{
    const auto mesh = generateAsteroidMesh(radius, 6, 10);
    std::vector<ToonVertex> out;
    out.reserve(mesh.indices.size());
    for (std::uint32_t index : mesh.indices)
    {
        const float* p = &mesh.vertices[index * 8];
        const math::Vec3f pos{p[0], p[1], p[2]};
        const math::Vec3f normal{p[4], p[5], p[6]};
        out.push_back({pos, normal, glm::normalize(pos)});
    }
    return out;
}

struct Scene
{
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<Allocator> allocator;
    std::unique_ptr<Image> color;
    std::unique_ptr<Image> depth;
    std::unique_ptr<Buffer> readback;
    Image::ImageView colorView;
    Image::ImageView depthView;
    std::optional<NebulaRenderer> nebula;
    std::optional<StarfieldRenderer> stars;
    std::optional<ToonRenderer> toon;
    std::vector<ToonMesh> meshes;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};

Scene makeScene()
{
    Scene s;
    ContextCreateInfo info;
    info.enableValidation = true;
    info.requiredExtensions.clear();
    auto contextResult = VulkanContext::create(info);
    REQUIRE(contextResult.has_value());
    s.context = std::make_unique<VulkanContext>(std::move(*contextResult));
    auto deviceResult = VulkanDevice::create(*s.context);
    REQUIRE(deviceResult.has_value());
    s.device = std::make_unique<VulkanDevice>(std::move(*deviceResult));
    auto allocatorResult = Allocator::create({
        .instance = s.context->instance(),
        .physicalDevice = s.context->physicalDevice(),
        .device = s.device->device(),
    });
    REQUIRE(allocatorResult.has_value());
    s.allocator = std::make_unique<Allocator>(std::move(*allocatorResult));

    auto colorResult = Image::create(*s.allocator, {
        .width = kW, .height = kH, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc});
    REQUIRE(colorResult.has_value());
    s.color = std::make_unique<Image>(std::move(*colorResult));
    auto colorViewResult = s.color->createImageView(s.device->device());
    REQUIRE(colorViewResult.has_value());
    s.colorView = std::move(*colorViewResult);

    auto depthResult = Image::create(*s.allocator, {
        .width = kW, .height = kH, .format = findDepthFormat(s.context->physicalDevice()),
        .usage = ImageUsageFlags::DepthAttachment});
    REQUIRE(depthResult.has_value());
    s.depth = std::make_unique<Image>(std::move(*depthResult));
    auto depthViewResult = s.depth->createImageView(s.device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
    REQUIRE(depthViewResult.has_value());
    s.depthView = std::move(*depthViewResult);

    auto readbackResult = Buffer::create(*s.allocator, {
        .size = kPixelBytes, .usage = BufferUsage::TransferDst,
        .memory = MemoryPlacement::GpuToCpu});
    REQUIRE(readbackResult.has_value());
    s.readback = std::make_unique<Buffer>(std::move(*readbackResult));

    const std::string dir = STARFORGE_SHADER_DIR;
    auto nebula = NebulaRenderer::create({
        .device = s.device->device(), .physicalDevice = s.context->physicalDevice(),
        .queueFamily = s.device->graphicsQueueFamily(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .depthFormat = VK_FORMAT_UNDEFINED,
        .shaderDirectory = dir});
    REQUIRE(nebula.has_value());
    s.nebula = std::move(*nebula);
    auto stars = StarfieldRenderer::create({
        .device = s.device->device(), .physicalDevice = s.context->physicalDevice(),
        .queueFamily = s.device->graphicsQueueFamily(),
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .depthFormat = VK_FORMAT_UNDEFINED,
        .shaderDirectory = dir});
    REQUIRE(stars.has_value());
    s.stars = std::move(*stars);
    auto toon = ToonRenderer::create({
        .instance = s.context->instance(), .device = s.device->device(),
        .physicalDevice = s.context->physicalDevice(),
        .queueFamily = s.device->graphicsQueueFamily(), .framesInFlight = 1,
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .depthFormat = findDepthFormat(s.context->physicalDevice()),
        .shaderDirectory = dir});
    REQUIRE(toon.has_value());
    s.toon = std::move(*toon);

    auto addMesh = [&](std::vector<ToonVertex> vertices) {
        auto mesh = s.toon->createMesh(vertices);
        REQUIRE(mesh.has_value());
        s.meshes.push_back(std::move(*mesh));
    };
    addMesh(makeShipHull());
    addMesh(makeCone(0.16f, 0.7f, 8));
    addMesh(makeSphere(8.0f, 12, 20));
    addMesh(makeAsteroid(0.5f));

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = s.device->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    REQUIRE(vkCreateCommandPool(s.device->device(), &poolInfo, nullptr, &s.pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = s.pool;
    alloc.commandBufferCount = 1;
    REQUIRE(vkAllocateCommandBuffers(s.device->device(), &alloc, &s.cmd) == VK_SUCCESS);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    REQUIRE(vkCreateFence(s.device->device(), &fenceInfo, nullptr, &s.fence) == VK_SUCCESS);
    return s;
}

void destroyScene(Scene& s)
{
    if (s.fence) vkDestroyFence(s.device->device(), s.fence, nullptr);
    if (s.cmd) vkFreeCommandBuffers(s.device->device(), s.pool, 1, &s.cmd);
    if (s.pool) vkDestroyCommandPool(s.device->device(), s.pool, nullptr);
    s.meshes.clear();
    s.toon.reset();
    s.stars.reset();
    s.nebula.reset();
    s.colorView = Image::ImageView{};
    s.depthView = Image::ImageView{};
    s.readback.reset();
    s.depth.reset();
    s.color.reset();
    s.allocator.reset();
    s.device.reset();
    s.context.reset();
}

void recordScene(Scene& s)
{
    vkResetCommandBuffer(s.cmd, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(vkBeginCommandBuffer(s.cmd, &begin) == VK_SUCCESS);

    s.color->transitionToColorAttachment(s.cmd);
    s.depth->transitionToDepthAttachment(s.cmd);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = s.colorView.handle();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = s.depthView.handle();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {kW, kH};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(s.cmd, &rendering);

    VkViewport viewport{0.0f, static_cast<float>(kH), static_cast<float>(kW),
                        -static_cast<float>(kH), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {kW, kH}};
    vkCmdSetViewport(s.cmd, 0, 1, &viewport);
    vkCmdSetScissor(s.cmd, 0, 1, &scissor);

    const math::Mat4f view = glm::lookAt(v3(0, 0, 6), v3(0, 0, 0), v3(0, 1, 0));
    const math::Mat4f proj = math::perspectiveVulkan(glm::radians(55.0f), 1.0f, 0.1f, 200.0f);
    const math::Mat4f viewProj = proj * view;

    NebulaRenderContext nebulaCtx{};
    nebulaCtx.viewProj = viewProj;
    nebulaCtx.invViewProj = glm::inverse(viewProj);
    nebulaCtx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
    NebulaParams nebulaParams{};
    nebulaParams.time = 0.5f;
    nebulaParams.baseColor = {0.18f, 0.05f, 0.35f, 1.0f};
    nebulaParams.density = 0.7f;
    nebulaParams.scattering = 0.5f;
    s.nebula->render(s.cmd, nebulaCtx, nebulaParams);

    StarfieldRenderContext starCtx{};
    starCtx.viewProj = viewProj;
    starCtx.invViewProj = nebulaCtx.invViewProj;
    starCtx.resolution = nebulaCtx.resolution;
    starCtx.time = 0.2f;
    StarfieldParams starParams{};
    starParams.intensity = 0.6f;
    starParams.worldPosition = {0.0f, 0.0f, 0.0f};
    starParams.radius = 1.0f;
    s.stars->render(s.cmd, starCtx, starParams);

    ToonRenderContext ctx{};
    ctx.viewProj = viewProj;
    ctx.resolution = {static_cast<float>(kW), static_cast<float>(kH)};
    ctx.lightDirectionView = v3(0.4f, 0.7f, 0.6f);
    ToonParams base{};
    base.bandCount = 4.0f;
    base.rimStrength = 0.6f;
    base.outlineWidth = 2.0f;

    auto draw = [&](std::size_t mesh, const math::Mat4f& modelView, ToonParams params) {
        ctx.modelView = modelView;
        s.toon->renderMesh(s.cmd, ctx, params, s.meshes[mesh]);
    };

    // Planet with atmospheric scattering rim.
    ToonParams planet = base;
    planet.baseColor = {0.1f, 0.35f, 0.6f, 1.0f};
    planet.shadowColor = {0.02f, 0.06f, 0.2f, 1.0f};
    planet.capabilities = 8;
    planet.fresnelColor = {0.4f, 0.7f, 1.0f, 0.8f};
    draw(2, glm::translate(math::Mat4f{1.0f}, v3(0.0f, 0.0f, -50.0f)), planet);

    // Outlined faceted asteroids.
    ToonParams rock = base;
    rock.baseColor = {0.45f, 0.38f, 0.3f, 1.0f};
    rock.shadowColor = {0.12f, 0.09f, 0.08f, 1.0f};
    rock.capabilities = 4;
    rock.materialParams.x = 0.8f;
    for (int i = 0; i < 3; ++i)
    {
        const float x = -1.8f + static_cast<float>(i) * 1.4f;
        draw(3, glm::translate(math::Mat4f{1.0f}, v3(x, -0.7f, -6.0f)), rock);
    }

    // Hero ship: outlined hull, four-stop palette, shadow/detail capabilities.
    ToonParams ship = base;
    ship.baseColor = {0.85f, 0.25f, 0.15f, 1.0f};
    ship.shadowColor = {0.18f, 0.03f, 0.08f, 1.0f};
    ship.highlightColor = {1.0f, 0.85f, 0.5f, 0.7f};
    ship.capabilities = 16;
    ship.materialParams = {0.0f, 0.5f, 0.004f, 0.5f};
    draw(0, glm::translate(math::Mat4f{1.0f}, v3(0.0f, 0.0f, 0.0f)), ship);

    // Glowing engine exhaust.
    ToonParams exhaust = base;
    exhaust.baseColor = {0.2f, 0.5f, 1.0f, 1.0f};
    exhaust.capabilities = 1;
    exhaust.emissiveColor = {0.4f, 1.2f, 2.0f, 1.4f};
    exhaust.outlineWidth = 0.0f;
    draw(1, glm::rotate(math::Mat4f{1.0f}, kPi, v3(0, 1, 0)) *
               glm::translate(math::Mat4f{1.0f}, v3(0.0f, 0.0f, -1.0f)), exhaust);

    // Weapon fire / impact: short emissive beam ahead of the ship.
    ToonParams weapon = base;
    weapon.capabilities = 1;
    weapon.emissiveColor = {2.0f, 1.4f, 0.3f, 1.2f};
    weapon.outlineWidth = 0.0f;
    draw(1, glm::translate(math::Mat4f{1.0f}, v3(0.55f, 0.0f, 2.0f)), weapon);

    vkCmdEndRendering(s.cmd);

    s.color->transitionToTransferSrc(s.cmd);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {kW, kH, 1};
    vkCmdCopyImageToBuffer(s.cmd, s.color->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           s.readback->handle(), 1, &copy);
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.buffer = s.readback->handle();
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(s.cmd, &dep);
    REQUIRE(vkEndCommandBuffer(s.cmd) == VK_SUCCESS);
}

void submitAndWait(Scene& s)
{
    REQUIRE(vkResetFences(s.device->device(), 1, &s.fence) == VK_SUCCESS);
    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = s.cmd;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdSubmit;
    REQUIRE(s.device->submit(1, &submit, s.fence) == VK_SUCCESS);
    REQUIRE(vkWaitForFences(s.device->device(), 1, &s.fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
}

} // namespace

TEST_CASE("M9.5.6: production toon showcase golden (T3)")
{
    auto scene = makeScene();
    recordScene(scene);
    submitAndWait(scene);

    auto mapped = scene.readback->map();
    REQUIRE(mapped.has_value());
    (void)scene.readback->invalidate(0, kPixelBytes);
    const auto* actual = static_cast<const std::uint8_t*>(*mapped);

    const std::string goldenPath = std::string(STARFORGE_GOLDEN_DIR) + "/toon_showcase_128.png";
    if (std::getenv("STARFORGE_UPDATE_GOLDENS") != nullptr)
    {
        REQUIRE(stbi_write_png(goldenPath.c_str(), kW, kH, 4, actual, kW * 4) == 1);
    }
    else
    {
        int gw = 0, gh = 0, gc = 0;
        unsigned char* golden = stbi_load(goldenPath.c_str(), &gw, &gh, &gc, 4);
        REQUIRE_MESSAGE(golden != nullptr, "Golden missing: " << goldenPath);
        REQUIRE(gw == static_cast<int>(kW));
        REQUIRE(gh == static_cast<int>(kH));
        std::uint32_t failing = 0;
        for (std::size_t i = 0; i < kPixelBytes; ++i)
        {
            if (std::abs(static_cast<int>(actual[i]) - static_cast<int>(golden[i])) > kTolerance) ++failing;
        }
        stbi_image_free(golden);
        CHECK(static_cast<double>(failing) / static_cast<double>(kPixelBytes) < 0.01);
    }
    std::size_t lit = 0;
    for (std::size_t i = 0; i < kPixelBytes; i += 4)
    {
        if (actual[i] + actual[i + 1] + actual[i + 2] > 60) ++lit;
    }
    scene.readback->unmap();
    CHECK(lit > 200); // ship, planet and effects are visibly present
    destroyScene(scene);
}

TEST_CASE("M9.5.6: production toon showcase frame budget (T5)")
{
    auto scene = makeScene();
    std::vector<double> timings;
    for (int frame = 0; frame < 30; ++frame)
    {
        recordScene(scene);
        const auto start = std::chrono::steady_clock::now();
        submitAndWait(scene);
        const auto end = std::chrono::steady_clock::now();
        if (frame >= 3) timings.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(timings.begin(), timings.end());
    const double p95 = timings[(timings.size() * 95) / 100];
    CHECK(p95 < 6.94);
    CHECK(scene.context->validationErrorCount() == 0);
    destroyScene(scene);
}
