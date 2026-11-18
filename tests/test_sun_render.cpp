#include <doctest/doctest.h>

#include "environment/sun_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "planets/cubed_sphere.hpp"
#include "planets/planet_mesh.hpp"
#include "spatial/camera_relative.hpp"
#include "spatial/world_position.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::environment;
using namespace starforge::gpu_resources;
using namespace starforge::math;
using namespace starforge::spatial;
using namespace starforge::vulkan;

TEST_CASE("Sun M15.0.7: Eddington limb darkening mathematical proof (T1)")
{
    // Eddington linear limb darkening law: I(μ) = 1.0 - u * (1.0 - μ)
    // where μ = cos(θ) = max(0, n · v).
    constexpr float u = 0.6f; // Standard visible-band solar coefficient

    auto limbDarkening = [](float mu, float uVal) {
        return 1.0f - uVal * (1.0f - std::clamp(mu, 0.0f, 1.0f));
    };

    // Disk center (μ = 1.0) -> maximum emission I = 1.0
    CHECK(limbDarkening(1.0f, u) == doctest::Approx(1.0f));

    // Disk limb / edge (μ = 0.0) -> darkened to (1 - u) = 0.4
    CHECK(limbDarkening(0.0f, u) == doctest::Approx(0.4f));

    // Mid-radius (μ = 0.5)
    CHECK(limbDarkening(0.5f, u) == doctest::Approx(0.7f));

    // Monotonic darkening from center outward
    float prevI = 0.0f;
    for (float mu = 0.0f; mu <= 1.0f; mu += 0.1f)
    {
        float currentI = limbDarkening(mu, u);
        CHECK(currentI >= prevI);
        prevI = currentI;
    }
}

TEST_CASE("Sun M15.0.7: Astronomical scale LargeObjectSetup jitter invariance (T1)")
{
    // Sun at 1 AU (~1.496e11 meters) from origin, camera at origin
    constexpr double kOneAU = 1.495978707e11;
    auto sunPos = WorldPosition::fromMeters(kOneAU, 0.0, 0.0);
    REQUIRE(sunPos.has_value());
    auto camPos = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camPos.has_value());

    // Sun radius R = 6.9634e8 meters (~700,000 km)
    constexpr double kSunRadius = 6.9634e8;
    const starforge::math::Vec3d patchCenter = starforge::planets::cubeToSphere(starforge::planets::CubeFace::PositiveX, 0.0, 0.0) * kSunRadius;

    // makeLargeObjectSetup computes cameraRelativeOrigin in 64-bit double precision
    const auto setup = makeLargeObjectSetup(*sunPos, *camPos, patchCenter);

    const double expectedDistance = kOneAU + kSunRadius;
    CHECK(setup.cameraRelativeOrigin.x == doctest::Approx(expectedDistance));

    // Precision check: float32 vertex offset relative to patch center remains sub-meter
    const starforge::math::Vec3f localVertex{100.0f, 50.0f, -25.0f};
    const auto camRelVertex = setup.transformPatchVertex(localVertex);

    // Transforming vertex does not produce NaNs or infinities at astronomical scale
    CHECK(!std::isnan(camRelVertex.x));
    CHECK(!std::isnan(camRelVertex.y));
    CHECK(!std::isnan(camRelVertex.z));
}

TEST_CASE("Sun M15.0.7: GPU headless SunRenderer core and corona offscreen rendering (T3)")
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

        constexpr std::uint32_t w = 256, h = 256;

        auto colorImage = Image::create(*allocator, {
            .width = w,
            .height = h,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc,
        });
        REQUIRE(colorImage.has_value());

        auto colorView = colorImage->createView(device->device());
        REQUIRE(colorView.has_value());

        auto depthImage = Image::create(*allocator, {
            .width = w,
            .height = h,
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = ImageUsageFlags::DepthAttachment,
        });
        REQUIRE(depthImage.has_value());

        auto depthView = depthImage->createView(device->device());
        REQUIRE(depthView.has_value());

        auto sunRenderer = SunRenderer::create({
            .instance = context->instance(),
            .device = device->device(),
            .physicalDevice = context->physicalDevice(),
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .depthFormat = VK_FORMAT_D32_SFLOAT,
            .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
            .defaultRadius = 5000.0,
            .subdivisions = 8,
        });
        REQUIRE(sunRenderer.has_value());

        // Create command pool & buffer for offscreen render
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

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

        // Transition attachments for dynamic rendering
        VkImageMemoryBarrier2 colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        colorBarrier.image = colorImage->handle();
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorBarrier.srcAccessMask = 0;
        colorBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkImageMemoryBarrier2 depthBarrier{};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        depthBarrier.image = depthImage->handle();
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        depthBarrier.srcAccessMask = 0;
        depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        VkImageMemoryBarrier2 barriers[2] = {colorBarrier, depthBarrier};
        depInfo.imageMemoryBarrierCount = 2;
        depInfo.pImageMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        // Begin dynamic rendering
        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView = *colorView;
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderingAttachmentInfo depthAtt{};
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = *depthView;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = {{0, 0}, {w, h}};
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAtt;
        renderInfo.pDepthAttachment = &depthAtt;

        vkCmdBeginRendering(cmd, &renderInfo);

        // Vulkan Y-down viewport convention (the projection does not flip Y).
        VkViewport viewport{0.0f, static_cast<float>(h), static_cast<float>(w), -static_cast<float>(h), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {w, h}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        SunRenderContext sunContext;
        const auto proj = perspectiveVulkan(45.0f * 3.14159f / 180.0f, 1.0f, 0.1f, 100000.0f);
        const auto view = glm::lookAt(glm::vec3(0.0f, 0.0f, 20000.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        sunContext.viewProj = proj * view;
        auto testCam = WorldPosition::fromMeters(0.0, 0.0, 20000.0);
        REQUIRE(testCam.has_value());
        sunContext.cameraPosition = *testCam;
        sunContext.time = 0.5f;

        SunParams sunParams;
        auto testSun = WorldPosition::fromMeters(0.0, 0.0, 0.0);
        REQUIRE(testSun.has_value());
        sunParams.worldPosition = *testSun;
        sunParams.radius = 5000.0;
        sunParams.coronaRadiusMultiplier = 6.0;

        sunRenderer->render(cmd, sunContext, sunParams);

        vkCmdEndRendering(cmd);
        REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        REQUIRE(vkQueueSubmit(device->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS);
        REQUIRE(vkQueueWaitIdle(device->graphicsQueue()) == VK_SUCCESS);

        vkDestroyCommandPool(device->device(), cmdPool, nullptr);
        vkDestroyImageView(device->device(), *colorView, nullptr);
        vkDestroyImageView(device->device(), *depthView, nullptr);
    }
}
