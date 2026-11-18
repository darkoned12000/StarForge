#include <doctest/doctest.h>

#include "bench/benchmark_scene.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "gpu_resources/gpu_culling.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <algorithm>
#include <cstdint>
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
constexpr std::uint32_t kInstances = 200;
constexpr float kRadius = 0.25f;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct DrawPush
{
    float    viewProj[16];
    std::uint64_t boundsAddress = 0;
    std::uint64_t visibleIndicesAddress = 0;
};
static_assert(sizeof(DrawPush) == 80);

VkShaderModule loadModule(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) return VK_NULL_HANDLE;
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<std::uint32_t> words(size / sizeof(std::uint32_t));
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

VkDeviceAddress addressOf(VkDevice device, const Buffer& buffer)
{
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer.handle();
    return vkGetBufferDeviceAddress(device, &info);
}

} // namespace

TEST_CASE("M5 Phase 3: GPU culling feeds an indirect cube draw (T3)")
{
    ContextCreateInfo contextInfo;
    contextInfo.enableValidation = kValidationDefault;
    contextInfo.requiredExtensions.clear();
    auto context = VulkanContext::create(contextInfo);
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
            // Two clusters: camera A sees only the left one, camera B only the right.
            std::vector<float> bounds(kInstances * 4);
            for (std::uint32_t i = 0; i < kInstances; ++i)
            {
                const float cx = (i < kInstances / 2) ? -3.0f : 3.0f;
                bounds[i * 4 + 0] = cx + static_cast<float>(i % 7) * 0.05f;
                bounds[i * 4 + 1] = static_cast<float>(i % 5) * 0.05f;
                bounds[i * 4 + 2] = 0.0f;
                bounds[i * 4 + 3] = kRadius;
            }

            auto culling = GpuCulling::create(*allocator, {
                .instanceCount = kInstances, .framesInFlight = 2, .name = "Phase3Culling"});
            REQUIRE(culling.has_value());
            REQUIRE(culling->uploadBounds(bounds).has_value());

            auto cullPipeline = ComputePipeline::createFromFile(
                *device, std::string(STARFORGE_SHADER_DIR) + "/cull.spv", "main", 3, 104);
            REQUIRE(cullPipeline.has_value());

            const std::string shaderDir = STARFORGE_SHADER_DIR;
            auto vert = loadModule(device->device(), shaderDir + "/indirect_cube.vert.spv");
            auto frag = loadModule(device->device(), shaderDir + "/indirect_cube.frag.spv");
            REQUIRE(vert != VK_NULL_HANDLE);
            REQUIRE(frag != VK_NULL_HANDLE);
            auto layout = createPushConstantPipelineLayout(device->device(), sizeof(DrawPush));
            REQUIRE(layout.has_value());
            auto graphics = GraphicsPipeline::create({
                .device = device->device(),
                .colorFormat = kColorFormat,
                .depthFormat = VK_FORMAT_UNDEFINED,
                .vertexShader = vert,
                .fragmentShader = frag,
                .pipelineLayout = *layout,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .depthTest = false,
                .depthWrite = false,
                .depthCompare = VK_COMPARE_OP_ALWAYS,
                .backfaceCulling = false,
                .viewportWidth = static_cast<float>(kWidth),
                .viewportHeight = static_cast<float>(kHeight),
            });
            REQUIRE(graphics.has_value());

            auto color = Image::create(*allocator, {
                .width = kWidth, .height = kHeight,
                .format = kColorFormat, .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferSrc,
            });
            REQUIRE(color.has_value());
            auto colorView = color->createImageView(device->device());
            REQUIRE(colorView.has_value());

            auto pixels = Buffer::create(*allocator, {
                .size = kPixelBytes, .usage = BufferUsage::TransferDst, .memory = MemoryPlacement::GpuToCpu});
            REQUIRE(pixels.has_value());
            auto commandReadback = Buffer::create(*allocator, {
                .size = 16, .usage = BufferUsage::TransferDst, .memory = MemoryPlacement::GpuToCpu});
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
            cmdAlloc.commandBufferCount = 2;
            VkCommandBuffer cmds[2] = {};
            REQUIRE(vkAllocateCommandBuffers(device->device(), &cmdAlloc, cmds) == VK_SUCCESS);

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence = VK_NULL_HANDLE;
            REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

            const VkDeviceAddress boundsAddress = addressOf(device->device(), culling->boundsBuffer());
            REQUIRE(boundsAddress != 0);

            struct Camera
            {
                Vec3d eye;
                Vec3d target;
            };
            const Camera cameras[2] = {
                {{0.0, 0.0, 6.0}, {-3.0, 0.0, 0.0}},
                {{0.0, 0.0, 6.0}, { 3.0, 0.0, 0.0}},
            };

            for (std::uint32_t slot = 0; slot < 2; ++slot)
            {
                const auto& camera = cameras[slot];
                const Mat4d view = glm::lookAt(camera.eye, camera.target, Vec3d(0, 1, 0));
                const Mat4d proj = Mat4d(perspectiveVulkan(glm::radians(35.0f), 1.0f, 0.1f, 100.0f));
                const Mat4d viewProj = proj * view;
                const auto frustum = starforge::bench::buildFrustum(viewProj);

                std::uint32_t expectedVisible = 0;
                float planes[24] = {};
                for (int p = 0; p < 6; ++p)
                {
                    for (int c = 0; c < 4; ++c) planes[p * 4 + c] = static_cast<float>(frustum.planes[p][c]);
                }
                for (std::uint32_t i = 0; i < kInstances; ++i)
                {
                    if (starforge::bench::sphereInFrustum(
                            frustum, bounds[i * 4], bounds[i * 4 + 1], bounds[i * 4 + 2], kRadius))
                    {
                        ++expectedVisible;
                    }
                }
                REQUIRE(expectedVisible > 0);
                REQUIRE(expectedVisible < kInstances); // each camera must reject the other cluster

                const VkCommandBuffer cmd = cmds[slot];
                REQUIRE(vkResetFences(device->device(), 1, &fence) == VK_SUCCESS);
                REQUIRE(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
                VkCommandBufferBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);

                // GPU cull (fill -> barrier -> dispatch -> barrier) in-place.
                REQUIRE(culling->recordCulling(cmd, device->device(), *cullPipeline, slot,
                                               planes, kInstances).has_value());

                // Draw the compacted instances with the GPU-written count.
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

                VkViewport viewport{};
                viewport.y = static_cast<float>(kHeight);
                viewport.width = static_cast<float>(kWidth);
                viewport.height = -static_cast<float>(kHeight);
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor{};
                scissor.extent = {kWidth, kHeight};
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                const Mat4f viewProjFloat = Mat4f(viewProj);
                DrawPush push{};
                std::memcpy(push.viewProj, glm::value_ptr(viewProjFloat), sizeof(push.viewProj));
                push.boundsAddress = boundsAddress;
                push.visibleIndicesAddress = addressOf(device->device(), culling->visibleIndicesBuffer(slot));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics->handle());
                vkCmdPushConstants(cmd, *layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(push), &push);
                vkCmdDrawIndirect(cmd, culling->indirectBuffer(slot).handle(), 0, 1, 0);
                vkCmdEndRendering(cmd);

                // Copy the GPU-written draw command for parity verification.
                VkBufferMemoryBarrier2 indirectToTransfer{};
                indirectToTransfer.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                indirectToTransfer.srcStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                indirectToTransfer.srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                indirectToTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                indirectToTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                indirectToTransfer.buffer = culling->indirectBuffer(slot).handle();
                indirectToTransfer.size = 16;
                VkDependencyInfo indirectDep{};
                indirectDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                indirectDep.bufferMemoryBarrierCount = 1;
                indirectDep.pBufferMemoryBarriers = &indirectToTransfer;
                vkCmdPipelineBarrier2(cmd, &indirectDep);
                VkBufferCopy commandCopy{};
                commandCopy.size = 16;
                vkCmdCopyBuffer(cmd, culling->indirectBuffer(slot).handle(),
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
                VkDependencyInfo hostDep{};
                hostDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                hostDep.bufferMemoryBarrierCount = 1;
                hostDep.pBufferMemoryBarriers = &hostBarrier;
                vkCmdPipelineBarrier2(cmd, &hostDep);

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
                CHECK(command[1] == expectedVisible); // GPU instanceCount == CPU oracle
                commandReadback->unmap();

                auto pixelMapped = pixels->map();
                REQUIRE(pixelMapped.has_value());
                (void)pixels->invalidate(0, kPixelBytes);
                const auto* bytes = static_cast<const std::uint8_t*>(*pixelMapped);
                std::size_t lit = 0;
                for (VkDeviceSize i = 0; i < kPixelBytes; i += 4)
                {
                    if (bytes[i] + bytes[i + 1] + bytes[i + 2] > 32) ++lit;
                }
                CHECK(lit > 100);
                pixels->unmap();
            }

            vkDestroyFence(device->device(), fence, nullptr);
            vkFreeCommandBuffers(device->device(), pool, 2, cmds);
            vkDestroyCommandPool(device->device(), pool, nullptr);
            vkDestroyShaderModule(device->device(), vert, nullptr);
            vkDestroyShaderModule(device->device(), frag, nullptr);
            vkDestroyPipelineLayout(device->device(), *layout, nullptr);
        }
        CHECK(allocator->allocationCount() == 0);
    }
    CHECK(context->validationErrorCount() == 0);
}
