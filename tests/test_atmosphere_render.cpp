#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "planets/atmosphere.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <optional>
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

// Must match PushConstants in atmosphere.slang (std430: float3 members
// are 16-byte aligned, so sunDir sits at offset 96; total 116 bytes).
struct PushData
{
    float viewProj[16];
    std::uint64_t vertexBufferAddress;
    std::uint64_t lutAddress;
    float cameraPos[3];
    float cameraPad = 0.0f;
    float sunDir[3];
    std::uint32_t lutWidth;
    std::uint32_t lutHeight;
};
// (116 bytes of shader data + 4 tail padding to 8-byte struct alignment;
// the push-constant range covers the full 120 bytes.)
static_assert(sizeof(PushData) == 120, "push constants must be 120 bytes");

} // namespace

TEST_CASE("Atmosphere render: LUT-sampled shell renders headlessly (7.4b)")
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
            constexpr float kShellRadius = 1100.0f;

            AtmosphereParams params{};
            params.planetRadius = 1000.0f;
            params.atmosphereRadius = kShellRadius;
            auto lut = generateAtmosphereLut(params, false);
            REQUIRE(lut.data.size() == lut.width * lut.height);

            // LUT texels as float4 (w unused).
            std::vector<float> lutData(lut.data.size() * 4);
            for (std::size_t i = 0; i < lut.data.size(); ++i)
            {
                lutData[i * 4 + 0] = lut.data[i][0];
                lutData[i * 4 + 1] = lut.data[i][1];
                lutData[i * 4 + 2] = lut.data[i][2];
                lutData[i * 4 + 3] = 1.0f;
            }
            auto lutBuffer = uploadBuffer(*allocator, lutData.data(), lutData.size() * sizeof(float));
            REQUIRE(lutBuffer.has_value());

            // UV sphere (interleaved float4 pos / float4 normal, stride 32).
            constexpr std::uint32_t kLat = 32;
            constexpr std::uint32_t kLon = 16;
            std::vector<float> vertices;
            std::vector<std::uint32_t> indices;
            vertices.reserve(static_cast<std::size_t>(kLat + 1) * (kLon + 1) * 8);
            for (std::uint32_t j = 0; j <= kLat; ++j)
            {
                const double theta = std::acos(-1.0) * static_cast<double>(j) / static_cast<double>(kLat);
                const double sinT = std::sin(theta);
                const double cosT = std::cos(theta);
                for (std::uint32_t i = 0; i <= kLon; ++i)
                {
                    const double phi = 2.0 * std::acos(-1.0) * static_cast<double>(i) / static_cast<double>(kLon);
                    const float nx = static_cast<float>(sinT * std::cos(phi));
                    const float ny = static_cast<float>(-cosT);
                    const float nz = static_cast<float>(sinT * std::sin(phi));
                    vertices.insert(vertices.end(),
                                    {nx * kShellRadius, ny * kShellRadius, nz * kShellRadius, 1.0f,
                                     nx, ny, nz, 0.0f});
                }
            }
            const std::uint32_t row = kLon + 1;
            for (std::uint32_t j = 0; j < kLat; ++j)
            {
                for (std::uint32_t i = 0; i < kLon; ++i)
                {
                    const std::uint32_t a = j * row + i;
                    const std::uint32_t b = a + 1;
                    const std::uint32_t c = a + row;
                    const std::uint32_t d = c + 1;
                    indices.insert(indices.end(), {a, c, b, b, c, d});
                }
            }
            auto vertexBuffer = uploadBuffer(*allocator, vertices.data(), vertices.size() * sizeof(float));
            REQUIRE(vertexBuffer.has_value());
            auto indexBuffer = uploadBuffer(*allocator, indices.data(), indices.size() * sizeof(std::uint32_t));
            REQUIRE(indexBuffer.has_value());

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
            VkShaderModule vertModule = loadShader(device->device(), shaderDir + "/atmosphere.vert.spv");
            VkShaderModule fragModule = loadShader(device->device(), shaderDir + "/atmosphere.frag.spv");
            REQUIRE(vertModule != VK_NULL_HANDLE);
            REQUIRE(fragModule != VK_NULL_HANDLE);

            // Vertex + fragment push constants (camera/sun/LUT addresses).
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            {
                VkPushConstantRange pushRange{};
                pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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
                    .depthWrite = false, // Atmosphere shell must not occlude terrain; additive blending TODO (helper has no blend state yet)
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

            const Vec3f cameraPos{0.0f, 0.0f, 3000.0f};
            glm::mat4 view = glm::lookAt(cameraPos, Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{0.0f, 1.0f, 0.0f});
            glm::mat4 proj = perspectiveVulkan(glm::radians(60.0f), 1.0f, 10.0f, 20000.0f);
            glm::mat4 viewProj = proj * view;

            PushData push{};
            std::memcpy(push.viewProj, glm::value_ptr(viewProj), sizeof(push.viewProj));
            push.vertexBufferAddress = vertexBuffer->deviceAddress(device->device());
            push.lutAddress = lutBuffer->deviceAddress(device->device());
            push.cameraPos[0] = cameraPos.x;
            push.cameraPos[1] = cameraPos.y;
            push.cameraPos[2] = cameraPos.z;
            // Sun toward the camera so most of the disc is on the day side.
            const Vec3f sunDir = glm::normalize(Vec3f{0.1f, 0.3f, 1.0f});
            push.sunDir[0] = sunDir.x;
            push.sunDir[1] = sunDir.y;
            push.sunDir[2] = sunDir.z;
            push.lutWidth = lut.width;
            push.lutHeight = lut.height;
            REQUIRE(push.vertexBufferAddress != 0);

            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);

            vkCmdBindIndexBuffer(cmd, indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
            pipeline->bindAndDraw(cmd, 0, static_cast<std::uint32_t>(indices.size()), 1);

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

            auto mapped = readback->map();
            REQUIRE(mapped.has_value());
            (void)readback->invalidate(0, w * h * 4);
            const auto* bytes = static_cast<const std::uint8_t*>(*mapped);

            std::uint32_t litPixels = 0;
            float centerSum = 0.0f;
            std::uint32_t centerCount = 0;
            float limbSum = 0.0f;
            std::uint32_t limbCount = 0;
            for (std::uint32_t y = 0; y < h; ++y)
            {
                for (std::uint32_t x = 0; x < w; ++x)
                {
                    const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                    const float lum =
                        static_cast<float>(bytes[i]) + static_cast<float>(bytes[i + 1]) + static_cast<float>(bytes[i + 2]);
                    if (lum > 0.0f)
                    {
                        ++litPixels;
                    }
                    const double dx = static_cast<double>(x) - 64.0;
                    const double dy = static_cast<double>(y) - 64.0;
                    const double r = std::sqrt(dx * dx + dy * dy);
                    if (r < 10.0)
                    {
                        centerSum += lum;
                        ++centerCount;
                    }
                    else if (r > 32.0 && r < 42.0)
                    {
                        limbSum += lum;
                        ++limbCount;
                    }
                }
            }

            // Shell disc renders; limb (longer in-shell path) outshines
            // the disc center. Disc covers ~1/3 of the frame; night-side
            // and high-altitude regions are legitimately dim, so the
            // threshold is well below the disc area.
            CHECK(litPixels > (w * h) / 10);
            REQUIRE(centerCount > 0);
            REQUIRE(limbCount > 0);
            CHECK(limbSum / static_cast<float>(limbCount) > centerSum / static_cast<float>(centerCount));

            readback->unmap();

            vkDestroyFence(device->device(), fence, nullptr);
            vkDestroyShaderModule(device->device(), vertModule, nullptr);
            vkDestroyShaderModule(device->device(), fragModule, nullptr);
            pipeline.reset();
            vkDestroyPipelineLayout(device->device(), pipelineLayout, nullptr);
            vkDestroyImageView(device->device(), *colorView, nullptr);
            vkDestroyImageView(device->device(), *depthView, nullptr);
            vkFreeCommandBuffers(device->device(), cmdPool, 1, &cmd);
            vkDestroyCommandPool(device->device(), cmdPool, nullptr);

            colorImage = std::expected<Image, std::string>();
            depthImage = std::expected<Image, std::string>();
            readback = std::expected<Buffer, std::string>();
            lutBuffer = std::expected<Buffer, std::string>();
            vertexBuffer = std::expected<Buffer, std::string>();
            indexBuffer = std::expected<Buffer, std::string>();
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
