#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "planets/planet_mesh.hpp"
#include "planets/planet_patch.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::gpu_resources;
using namespace starforge::math;
using namespace starforge::planets;
using namespace starforge::vulkan;

namespace
{

std::vector<char> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) return {};
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VkShaderModule loadShader(VkDevice device, const std::string& path)
{
    auto spv = readFile(path);
    if (spv.empty()) return VK_NULL_HANDLE;
    // SPIR-V must be word-aligned with a 5-word header; reject truncated
    // files before copying (avoids heap overflow on corrupt input).
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;
    if (spv.size() < 20 || (spv.size() % sizeof(std::uint32_t)) != 0) return VK_NULL_HANDLE;
    std::uint32_t magic = 0;
    std::memcpy(&magic, spv.data(), sizeof(magic));
    if (magic != kSpirvMagic) return VK_NULL_HANDLE;
    std::vector<std::uint32_t> aligned(spv.size() / sizeof(std::uint32_t));
    std::memcpy(aligned.data(), spv.data(), spv.size());
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = aligned.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

// Upload helper: device buffer via CpuToGpu mapping.
std::expected<Buffer, std::string> uploadBuffer(Allocator& allocator, const void* data, std::size_t byteSize)
{
    auto buffer = Buffer::create(allocator, {
        .size = static_cast<VkDeviceSize>(byteSize),
        .usage = BufferUsage::Storage,
        .memory = MemoryPlacement::CpuToGpu,
    });
    if (!buffer)
    {
        return std::unexpected(buffer.error());
    }
    auto mapped = buffer->map();
    if (!mapped)
    {
        return std::unexpected(mapped.error());
    }
    std::memcpy(*mapped, data, byteSize);
    (void)buffer->flush(0, static_cast<VkDeviceSize>(byteSize));
    buffer->unmap();
    return buffer;
}

// Must match vertex format in planet_patch.slang: float4 position /
// float4 normal / float2 uv interleaved (std430-safe stride 48).
struct CpuVertex
{
    float pos[4];
    float normal[4];
    float uv[2];
    float uvPad[2] = {};
};
static_assert(sizeof(CpuVertex) == 48, "interleaved vertex must be 48 bytes");

struct PushData
{
    float viewProj[16];
    std::uint64_t vertexBufferAddress;
};
static_assert(sizeof(PushData) == 72, "push constants must be 72 bytes");

} // namespace

TEST_CASE("Planet patch render: quadtree patch renders headlessly (7.2b)")
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
            constexpr std::uint32_t w = 128, h = 128;
            constexpr double kRadius = 1000.0;

            // One root patch on the +X face, subdivided 8x8.
            PlanetPatchGenerator gen(kRadius, 1.0);
            auto patch = gen.createRootPatch(CubeFace::PositiveX);
            auto mesh = generatePatchMesh(patch, 8, kRadius);
            REQUIRE(!mesh.positions.empty());

            // Interleave into a single packed vertex buffer (32 B/vertex);
            // resize once and assign by index (no per-vertex inserts).
            std::vector<CpuVertex> vertices(mesh.positions.size());
            for (std::size_t i = 0; i < mesh.positions.size(); ++i)
            {
                vertices[i].pos[0] = mesh.positions[i].x;
                vertices[i].pos[1] = mesh.positions[i].y;
                vertices[i].pos[2] = mesh.positions[i].z;
                vertices[i].pos[3] = 1.0f;
                vertices[i].normal[0] = mesh.normals[i].x;
                vertices[i].normal[1] = mesh.normals[i].y;
                vertices[i].normal[2] = mesh.normals[i].z;
                vertices[i].normal[3] = 0.0f;
                vertices[i].uv[0] = mesh.uvs[i].x;
                vertices[i].uv[1] = mesh.uvs[i].y;
            }

            auto vertexBuffer = uploadBuffer(*allocator, vertices.data(), vertices.size() * sizeof(CpuVertex));
            REQUIRE(vertexBuffer.has_value());
            auto indexBuffer = uploadBuffer(*allocator, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
            REQUIRE(indexBuffer.has_value());

            // Offscreen targets.
            auto colorImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .usage = ImageUsageFlags::ColorAttachment,
            });
            REQUIRE(colorImage.has_value());
            auto colorView = colorImage->createView(device->device());
            REQUIRE(colorView.has_value());

            VkFormat depthFormat = findDepthFormat(context->physicalDevice());
            REQUIRE(depthFormat != VK_FORMAT_UNDEFINED);
            auto depthImage = Image::create(*allocator, {
                .width = w,
                .height = h,
                .format = depthFormat,
                .usage = ImageUsageFlags::DepthAttachment,
            });
            REQUIRE(depthImage.has_value());
            auto depthView = depthImage->createView(device->device(), VK_IMAGE_ASPECT_DEPTH_BIT);
            REQUIRE(depthView.has_value());

            auto readback = Buffer::create(*allocator, {
                .size = w * h * 4,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            std::string shaderDir = std::string(STARFORGE_SHADER_DIR);
            VkShaderModule vertModule = loadShader(device->device(), shaderDir + "/planet_patch.vert.spv");
            VkShaderModule fragModule = loadShader(device->device(), shaderDir + "/planet_patch.frag.spv");
            REQUIRE(vertModule != VK_NULL_HANDLE);
            REQUIRE(fragModule != VK_NULL_HANDLE);

            // Push constants are consumed by the vertex stage only.
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            {
                VkPushConstantRange pushRange{};
                pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                pushRange.offset = 0;
                pushRange.size = sizeof(PushData);
                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.pushConstantRangeCount = 1;
                layoutInfo.pPushConstantRanges = &pushRange;
                REQUIRE(vkCreatePipelineLayout(device->device(), &layoutInfo, nullptr, &pipelineLayout) == VK_SUCCESS);
            }

            std::optional<GraphicsPipeline> pipeline;
            {
                auto created = GraphicsPipeline::create({
                    .device = device->device(),
                    .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                    .depthFormat = depthFormat,
                    .vertexShader = vertModule,
                    .fragmentShader = fragModule,
                    .pipelineLayout = pipelineLayout,
                    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                    .depthTest = true,
                    .depthWrite = true,
                    .depthCompare = VK_COMPARE_OP_LESS,
                    .backfaceCulling = false,
                    .viewportWidth = static_cast<float>(w),
                    .viewportHeight = static_cast<float>(h),
                });
                REQUIRE(created.has_value());
                pipeline = std::move(*created);
            }

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = device->graphicsQueueFamily();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            VkCommandPool cmdPool = VK_NULL_HANDLE;
            REQUIRE(vkCreateCommandPool(device->device(), &poolInfo, nullptr, &cmdPool) == VK_SUCCESS);

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = cmdPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            REQUIRE(vkAllocateCommandBuffers(device->device(), &allocInfo, &cmd) == VK_SUCCESS);

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence = VK_NULL_HANDLE;
            REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);

            // Render from several altitudes (descent proof, 7.2c):
            // camera approaches the patch center; every altitude must
            // render the patch and stay bounded (single patch, 81 verts).
            const double altitudes[] = {3000.0, 2000.0, 1300.0};
            for (double alt : altitudes)
            {
                // Camera-relative view: eye = patch.center + offset,
                // looking at the patch center (origin in RTC space).
                const Vec3f eye{static_cast<float>(alt), static_cast<float>(alt * 0.25), 0.0f};
                glm::mat4 view = glm::lookAt(eye, Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{0.0f, 1.0f, 0.0f});
                glm::mat4 proj = perspectiveVulkan(glm::radians(60.0f), 1.0f, 10.0f, 20000.0f);
                glm::mat4 viewProj = proj * view;

                PushData push{};
                std::memcpy(push.viewProj, glm::value_ptr(viewProj), sizeof(push.viewProj));
                push.vertexBufferAddress = vertexBuffer->deviceAddress(device->device());
                REQUIRE(push.vertexBufferAddress != 0);

                REQUIRE(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);

                Image::transitionToColorAttachment(cmd, colorImage->handle());
                Image::transitionLayout(cmd, depthImage->handle(),
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

                VkRenderingAttachmentInfo colorAttachment{};
                colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colorAttachment.imageView = *colorView;
                colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};

                VkRenderingAttachmentInfo depthAttachment{};
                depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAttachment.imageView = *depthView;
                depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                depthAttachment.clearValue.depthStencil = VkClearDepthStencilValue{1.0f, 0};

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
                viewport.y = static_cast<float>(h);
                viewport.width = static_cast<float>(w);
                viewport.height = -static_cast<float>(h);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor{};
                scissor.offset = {0, 0};
                scissor.extent = {w, h};
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(push), &push);

                // Hardware indexed draw: SV_VertexID is dereferenced by
                // the indexer, so the vertex shader reads vertices directly.
                vkCmdBindIndexBuffer(cmd, indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
                pipeline->bindAndDraw(cmd, 0, static_cast<std::uint32_t>(mesh.indices.size()), 1);

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

                REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);
                REQUIRE(vkResetFences(device->device(), 1, &fence) == VK_SUCCESS);

                VkCommandBufferSubmitInfo cmdSubmit{};
                cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                cmdSubmit.commandBuffer = cmd;
                VkSubmitInfo2 submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
                submitInfo.commandBufferInfoCount = 1;
                submitInfo.pCommandBufferInfos = &cmdSubmit;
                REQUIRE(vkQueueSubmit2(device->graphicsQueue(), 1, &submitInfo, fence) == VK_SUCCESS);
                REQUIRE(vkWaitForFences(device->device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

                auto mapped = readback->map();
                REQUIRE(mapped.has_value());
                (void)readback->invalidate(0, w * h * 4);
                const auto* bytes = static_cast<const std::uint8_t*>(*mapped);
                std::uint32_t litPixels = 0;
                for (std::size_t i = 0; i < w * h * 4; i += 4)
                {
                    if (bytes[i] != 0 || bytes[i + 1] != 0 || bytes[i + 2] != 0)
                    {
                        ++litPixels;
                    }
                }
                // Patch is visible at every altitude (smallest at the
                // farthest, still a few percent of the frame).
                CHECK(litPixels > (w * h) / 20);
                readback->unmap();
            }

            vkDestroyFence(device->device(), fence, nullptr);
            vkDestroyShaderModule(device->device(), vertModule, nullptr);
            vkDestroyShaderModule(device->device(), fragModule, nullptr);
            // Destroy the pipeline before the layout it depends on.
            pipeline.reset();
            vkDestroyPipelineLayout(device->device(), pipelineLayout, nullptr);
            vkDestroyImageView(device->device(), *colorView, nullptr);
            vkDestroyImageView(device->device(), *depthView, nullptr);
            vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
            vkDestroyCommandPool(device->device(), cmdPool, nullptr);

            colorImage = std::expected<Image, std::string>();
            depthImage = std::expected<Image, std::string>();
            readback = std::expected<Buffer, std::string>();
            vertexBuffer = std::expected<Buffer, std::string>();
            indexBuffer = std::expected<Buffer, std::string>();
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
