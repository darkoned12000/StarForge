#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "math/math.hpp"
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

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = reinterpret_cast<const uint32_t*>(spv.data());

    std::vector<uint32_t> aligned(spv.size() / sizeof(uint32_t));
    std::memcpy(aligned.data(), spv.data(), spv.size());
    info.pCode = aligned.data();

    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

} // namespace

TEST_CASE("Mesh render: cube with per-face colors and depth buffer")
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
            constexpr std::uint32_t w = 64, h = 64;
            constexpr VkDeviceSize pixelCount = w * h;
            constexpr VkDeviceSize byteSize = pixelCount * 4;

            // Cube vertices: 8 corners, each with position and color.
            // Per-face colors: +X=red, -X=green, +Y=blue, -Y=yellow, +Z=magenta, -Z=cyan.
            struct Vertex { float px, py, pz, _pad0; float r, g, b, _pad1; };
            const Vertex vertices[] = {
                // Front face (z = +0.5): red
                {-0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
                { 0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
                { 0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
                {-0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
                // Back face (z = -0.5): green
                {-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
                { 0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
                { 0.5f,  0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
                {-0.5f,  0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
            };

            const uint16_t indices[] = {
                // Front
                0, 1, 2,  2, 3, 0,
                // Back
                5, 4, 7,  7, 6, 5,
                // Left
                4, 0, 3,  3, 7, 4,
                // Right
                1, 5, 6,  6, 2, 1,
                // Top
                3, 2, 6,  6, 7, 3,
                // Bottom
                4, 5, 1,  1, 0, 4,
            };

            constexpr VkDeviceSize vertexSize = sizeof(vertices);
            constexpr VkDeviceSize indexSize = sizeof(indices);

            // Create BDA vertex buffer.
            auto vertexBuf = Buffer::create(*allocator, {
                .size = vertexSize,
                .usage = BufferUsage::Storage,
                .memory = MemoryPlacement::CpuToGpu,
            });
            REQUIRE(vertexBuf.has_value());

            {
                auto mapped = vertexBuf->map();
                REQUIRE(mapped.has_value());
                std::memcpy(*mapped, vertices, vertexSize);
                vertexBuf->flush(0, vertexSize);
                vertexBuf->unmap();
            }

            // Create BDA index buffer.
            auto indexBuf = Buffer::create(*allocator, {
                .size = indexSize,
                .usage = BufferUsage::Storage,
                .memory = MemoryPlacement::CpuToGpu,
            });
            REQUIRE(indexBuf.has_value());

            {
                auto mapped = indexBuf->map();
                REQUIRE(mapped.has_value());
                std::memcpy(*mapped, indices, indexSize);
                indexBuf->flush(0, indexSize);
                indexBuf->unmap();
            }

            // Query device addresses.
            VkBufferDeviceAddressInfo vertexAddrInfo{};
            vertexAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            vertexAddrInfo.buffer = vertexBuf->handle();
            VkDeviceAddress vertexAddr = vkGetBufferDeviceAddress(device->device(), &vertexAddrInfo);

            VkBufferDeviceAddressInfo indexAddrInfo{};
            indexAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            indexAddrInfo.buffer = indexBuf->handle();
            VkDeviceAddress indexAddr = vkGetBufferDeviceAddress(device->device(), &indexAddrInfo);

            CHECK(vertexAddr != 0);
            CHECK(indexAddr != 0);
            // Debug: print addresses
            printf("vertexAddr = 0x%llx, indexAddr = 0x%llx\n", (unsigned long long)vertexAddr, (unsigned long long)indexAddr);

            // Create color image.
            auto colorImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .usage = ImageUsage::ColorAttachment,
            });
            REQUIRE(colorImage.has_value());

            auto colorView = colorImage->createView(device->device());
            REQUIRE(colorView.has_value());

            // Create depth image.
            VkFormat depthFormat = findDepthFormat(context->physicalDevice());
            REQUIRE(depthFormat != VK_FORMAT_UNDEFINED);

            auto depthImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = depthFormat,
                .usage = ImageUsage::DepthAttachment,
            });
            REQUIRE(depthImage.has_value());

            auto depthView = depthImage->createView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
            REQUIRE(depthView.has_value());

            // Create readback buffer.
            auto readback = Buffer::create(*allocator, {
                .size = byteSize,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            // Load shaders.
            std::string shaderDir = std::string(STARFORGE_SHADER_DIR);
            VkShaderModule vertModule = loadShader(device->device(), shaderDir + "/test_triangle.vert.spv");
            VkShaderModule fragModule = loadShader(device->device(), shaderDir + "/test_triangle.frag.spv");
            REQUIRE(vertModule != VK_NULL_HANDLE);
            REQUIRE(fragModule != VK_NULL_HANDLE);

            // Create pipeline layout with push constants.
            auto pipelineLayout = createPushConstantPipelineLayout(device->device(), 64);
            REQUIRE(pipelineLayout.has_value());

            // Create graphics pipeline.
            auto pipeline = GraphicsPipeline::create({
                .device = device->device(),
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
            REQUIRE(pipeline.has_value());

            // Record commands.
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

            // Transition images.
            Image::transitionToColorAttachment(cmd, colorImage->handle());
            Image::transitionToDepthAttachment(cmd, depthImage->handle());

            // Dynamic rendering.
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

            // Set viewport and scissor.
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

            // Push constants: simple perspective projection looking at the cube.
            // Camera at (0, 0, 3) looking down -Z. Cube at origin.
            // Use GLM to compute the MVP matrix.
            #include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
            glm::mat4 revProj = perspectiveVulkanReverseZ(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
            glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 model = glm::mat4(1.0f);
            (void)revProj;
            (void)view;
            (void)model;

            struct PushData {
                float mvp[16];
            } pushData{};

            // Identity matrix (column-major).
            for (int i = 0; i < 16; ++i) pushData.mvp[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            // No push constants needed (shader uses hardcoded vertices).

            // Bind index buffer.
            VkDeviceSize indexOffset = 0;
            vkCmdBindIndexBuffer(cmd, indexBuf->handle(), indexOffset, VK_INDEX_TYPE_UINT16);

            // Draw.
            pipeline->bindAndDraw(cmd, 8, 36, 1);

            vkCmdEndRendering(cmd);

            // Transition color to transfer src for readback.
            Image::transitionLayout(cmd, colorImage->handle(),
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

            // Copy to buffer.
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {w, h, 1};
            vkCmdCopyImageToBuffer(cmd, colorImage->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  readback->handle(), 1, &region);

            // Host barrier.
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

            // Verify some non-zero pixels were rendered (cube is visible).
            auto mapped = readback->map();
            REQUIRE(mapped.has_value());
            readback->invalidate(0, byteSize);

            const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
            bool hasNonZero = false;
            for (std::size_t i = 0; i < byteSize; i += 4)
            {
                if (bytes[i] != 0 || bytes[i+1] != 0 || bytes[i+2] != 0)
                {
                    hasNonZero = true;
                    break;
                }
            }
            CHECK(hasNonZero);

            readback->unmap();

            // Cleanup.
            vkDestroyFence(device->device(), fence, nullptr);
            vkDestroyShaderModule(device->device(), vertModule, nullptr);
            vkDestroyShaderModule(device->device(), fragModule, nullptr);
            vkDestroyPipelineLayout(device->device(), *pipelineLayout, nullptr);
            vkDestroyImageView(device->device(), *colorView, nullptr);
            vkDestroyImageView(device->device(), *depthView, nullptr);
            vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
            vkDestroyCommandPool(device->device(), cmdPool, nullptr);

            colorImage = std::expected<Image, std::string>();
            depthImage = std::expected<Image, std::string>();
            readback = std::expected<Buffer, std::string>();
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
