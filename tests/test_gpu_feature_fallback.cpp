#include <doctest/doctest.h>

#include "bench/benchmark_scene.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "gpu_resources/gpu_culling.hpp"
#include "gpu_resources/gpu_driven_draw.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_capabilities.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;
using namespace starforge::math;

namespace
{

constexpr std::uint32_t kWidth = 128;
constexpr std::uint32_t kHeight = 128;
constexpr VkDeviceSize kPixelBytes = kWidth * kHeight * 4;
constexpr std::uint32_t kInstances = 100;
constexpr std::uint32_t kVisible = 50;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

class FeatureEnv
{
public:
    explicit FeatureEnv(const char* value)
    {
        const char* previous = std::getenv(starforge::vulkan::kDisableFeaturesEnvVar);
        previous_ = previous ? std::string(previous) : std::string();
        hadPrevious_ = previous != nullptr;
        if (value != nullptr && *value != '\0')
        {
            setenv(starforge::vulkan::kDisableFeaturesEnvVar, value, 1);
        }
        else
        {
            unsetenv(starforge::vulkan::kDisableFeaturesEnvVar);
        }
    }
    ~FeatureEnv()
    {
        if (hadPrevious_)
        {
            setenv(starforge::vulkan::kDisableFeaturesEnvVar, previous_.c_str(), 1);
        }
        else
        {
            unsetenv(starforge::vulkan::kDisableFeaturesEnvVar);
        }
    }
    FeatureEnv(const FeatureEnv&) = delete;
    FeatureEnv& operator=(const FeatureEnv&) = delete;

private:
    std::string previous_;
    bool hadPrevious_ = false;
};

void runFallbackCase(const char* disabledFeatures,
                     bool expectDescriptorFallback,
                     bool expectDrawIndirectFallback)
{
    FeatureEnv env(disabledFeatures);

    ContextCreateInfo contextInfo;
    contextInfo.enableValidation = kValidationDefault;
    contextInfo.requiredExtensions.clear();
    auto context = VulkanContext::create(contextInfo);
    REQUIRE(context.has_value());

    const auto& caps = context->capabilities();
    CHECK(caps.bindless == !expectDescriptorFallback);
    CHECK(caps.drawIndirectCount == !expectDrawIndirectFallback);

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
            std::vector<float> bounds(kInstances * 4);
            for (std::uint32_t i = 0; i < kInstances; ++i)
            {
                bounds[i * 4 + 0] = (i < kVisible) ? static_cast<float>(i % 7) * 0.3f - 0.9f : 50.0f;
                bounds[i * 4 + 1] = (i < kVisible) ? static_cast<float>(i % 5) * 0.3f - 0.6f : 0.0f;
                bounds[i * 4 + 2] = 0.0f;
                bounds[i * 4 + 3] = 0.4f;
            }

            auto culling = GpuCulling::create(*allocator, {
                .instanceCount = kInstances, .framesInFlight = 1});
            REQUIRE(culling.has_value());
            REQUIRE(culling->uploadBounds(bounds).has_value());

            std::ifstream shader(std::string(STARFORGE_SHADER_DIR) + "/cull.spv",
                                 std::ios::ate | std::ios::binary);
            REQUIRE(shader.good());
            const auto spirvSize = static_cast<std::size_t>(shader.tellg());
            std::vector<std::uint8_t> spirv(spirvSize);
            shader.seekg(0);
            shader.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(spirvSize));
            auto cullPipeline = ComputePipeline::create(*device, {
                .spirv = spirv, .entryPoint = "main", .storageBufferCount = 3,
                .pushConstantSize = 104});
            REQUIRE(cullPipeline.has_value());

            auto draw = GpuDrivenDraw::create({
                .device = device->device(),
                .physicalDevice = context->physicalDevice(),
                .queueFamily = device->graphicsQueueFamily(),
                .colorFormat = kColorFormat,
                .width = kWidth,
                .height = kHeight,
                .shaderDirectory = STARFORGE_SHADER_DIR,
                .bindless = caps.bindless,
                .drawIndirectCount = caps.drawIndirectCount,
            });
            REQUIRE(draw.has_value());
            CHECK(draw->usingDescriptorFallback() == expectDescriptorFallback);
            CHECK(draw->usingDrawIndirectFallback() == expectDrawIndirectFallback);

            auto color = Image::create(*allocator, {
                .width = kWidth, .height = kHeight,
                .format = kColorFormat,
                .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc,
            });
            REQUIRE(color.has_value());
            auto colorView = color->createImageView(device->device());
            REQUIRE(colorView.has_value());
            auto pixels = Buffer::create(*allocator, {
                .size = kPixelBytes, .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu});
            REQUIRE(pixels.has_value());
            auto commandReadback = Buffer::create(*allocator, {
                .size = 16, .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu});
            REQUIRE(commandReadback.has_value());

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

            const Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
            const Mat4d proj = Mat4d(perspectiveVulkan(glm::radians(60.0f), 1.0f, 0.1f, 100.0f));
            const auto frustum = starforge::bench::buildFrustum(proj * view);
            float planes[24] = {};
            for (int p = 0; p < 6; ++p)
                for (int c = 0; c < 4; ++c)
                    planes[p * 4 + c] = static_cast<float>(frustum.planes[p][c]);

            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
            REQUIRE(culling->recordCulling(cmd, device->device(), *cullPipeline, 0,
                                           planes, kInstances).has_value());

            color->transitionToColorAttachment(cmd);
            VkRenderingAttachmentInfo attachment{};
            attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView = colorView->handle();
            attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea.extent = {kWidth, kHeight};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &attachment;
            vkCmdBeginRendering(cmd, &rendering);

            const Mat4f projFloat = Mat4f(proj * view);
            float viewProj[16] = {};
            std::memcpy(viewProj, glm::value_ptr(projFloat), sizeof(viewProj));
            auto addressOf = [&](VkBuffer buffer) {
                VkBufferDeviceAddressInfo info{};
                info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                info.buffer = buffer;
                return vkGetBufferDeviceAddress(device->device(), &info);
            };
            draw->record(cmd, culling->indirectBuffer(0).handle(),
                         culling->boundsBuffer().handle(),
                         culling->visibleIndicesBuffer(0).handle(),
                         viewProj,
                         addressOf(culling->boundsBuffer().handle()),
                         addressOf(culling->visibleIndicesBuffer(0).handle()));
            vkCmdEndRendering(cmd);

            VkBufferCopy commandCopy{};
            commandCopy.size = 16;
            vkCmdCopyBuffer(cmd, culling->indirectBuffer(0).handle(),
                            commandReadback->handle(), 1, &commandCopy);
            VkBufferImageCopy imageCopy{};
            imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageCopy.imageSubresource.layerCount = 1;
            imageCopy.imageExtent = {kWidth, kHeight, 1};
            color->transitionToTransferSrc(cmd);
            vkCmdCopyImageToBuffer(cmd, color->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   pixels->handle(), 1, &imageCopy);
            VkBufferMemoryBarrier2 hostBarrier{};
            hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            hostBarrier.buffer = pixels->handle();
            hostBarrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = 1;
            dep.pBufferMemoryBarriers = &hostBarrier;
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

            auto commandMapped = commandReadback->map();
            REQUIRE(commandMapped.has_value());
            (void)commandReadback->invalidate(0, 16);
            const auto* command = static_cast<const std::uint32_t*>(*commandMapped);
            CHECK(command[0] == 36);
            CHECK(command[1] == kVisible);
            commandReadback->unmap();

            auto pixelMapped = pixels->map();
            REQUIRE(pixelMapped.has_value());
            (void)pixels->invalidate(0, kPixelBytes);
            const auto* bytes = static_cast<const std::uint8_t*>(*pixelMapped);
            std::size_t lit = 0;
            for (VkDeviceSize i = 0; i < kPixelBytes; i += 4)
                if (bytes[i] + bytes[i + 1] + bytes[i + 2] > 32) ++lit;
            CHECK(lit > 100);
            pixels->unmap();

            const auto expectedKind = expectDrawIndirectFallback
                                          ? GpuDrivenDraw::DrawCallKind::DrawIndirect
                                          : GpuDrivenDraw::DrawCallKind::DrawIndirectCount;
            CHECK(draw->lastDrawCallKind() == expectedKind);
            if (expectDescriptorFallback)
            {
                CHECK(draw->descriptorUpdateCount() == 1); // set up once, reused
            }
            else
            {
                CHECK(draw->descriptorUpdateCount() == 0); // BDA path uses no descriptors
            }

            vkDestroyFence(device->device(), fence, nullptr);
            vkFreeCommandBuffers(device->device(), pool, 1, &cmd);
            vkDestroyCommandPool(device->device(), pool, nullptr);
        }
        CHECK(allocator->allocationCount() == 0);
    }
    CHECK(context->validationErrorCount() == 0);
}

} // namespace

TEST_CASE("Fallback: full GPU-driven draw with all optional features enabled")
{
    runFallbackCase(nullptr, false, false);
}

TEST_CASE("Fallback: drawIndirectCount disabled uses vkCmdDrawIndirect")
{
    runFallbackCase("drawIndirectCount", false, true);
}

TEST_CASE("Fallback: bindless disabled uses descriptor storage buffers")
{
    runFallbackCase("bindless", true, false);
}

TEST_CASE("Fallback: both optional features disabled")
{
    runFallbackCase("drawIndirectCount,bindless", true, true);
}
