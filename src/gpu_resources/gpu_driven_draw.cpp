#include "gpu_resources/gpu_driven_draw.hpp"

#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"

#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::gpu_resources
{

namespace
{

VkShaderModule loadShader(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) return VK_NULL_HANDLE;
    const auto size = static_cast<std::size_t>(file.tellg());
    if (size == 0 || (size % sizeof(std::uint32_t)) != 0) return VK_NULL_HANDLE;
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

} // namespace

std::expected<GpuDrivenDraw, std::string>
GpuDrivenDraw::create(const GpuDrivenDrawCreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.width == 0 || info.height == 0)
    {
        return std::unexpected("GpuDrivenDraw requires a device and non-zero extent");
    }

    GpuDrivenDraw draw;
    draw.device_ = info.device;
    draw.width_ = info.width;
    draw.height_ = info.height;
    draw.bindless_ = info.bindless;
    draw.drawIndirectCount_ = info.drawIndirectCount;

    const std::string stage = draw.bindless_ ? "indirect_cube" : "indirect_cube_descriptors";
    draw.vertexModule_ = loadShader(info.device, info.shaderDirectory + "/" + stage + ".vert.spv");
    draw.fragmentModule_ = loadShader(info.device, info.shaderDirectory + "/" + stage + ".frag.spv");
    if (draw.vertexModule_ == VK_NULL_HANDLE || draw.fragmentModule_ == VK_NULL_HANDLE)
    {
        draw.destroy();
        return std::unexpected("Failed to load GpuDrivenDraw shader artifacts");
    }

    if (!draw.bindless_)
    {
        VkDescriptorSetLayoutBinding bindings[2]{};
        for (std::uint32_t i = 0; i < 2; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        if (const VkResult result = vkCreateDescriptorSetLayout(
                info.device, &layoutInfo, nullptr, &draw.descriptorSetLayout_); result != VK_SUCCESS)
        {
            draw.destroy();
            return std::unexpected(vulkan::describeFailure("vkCreateDescriptorSetLayout", result));
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 2;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (const VkResult result = vkCreateDescriptorPool(
                info.device, &poolInfo, nullptr, &draw.descriptorPool_); result != VK_SUCCESS)
        {
            draw.destroy();
            return std::unexpected(vulkan::describeFailure("vkCreateDescriptorPool", result));
        }

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = draw.descriptorPool_;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &draw.descriptorSetLayout_;
        if (const VkResult result = vkAllocateDescriptorSets(
                info.device, &allocateInfo, &draw.descriptorSet_); result != VK_SUCCESS)
        {
            draw.destroy();
            return std::unexpected(vulkan::describeFailure("vkAllocateDescriptorSets", result));
        }
    }

    const std::uint32_t pushSize = draw.bindless_ ? 80u : 64u;
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.size = pushSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = draw.bindless_ ? 0u : 1u;
    layoutInfo.pSetLayouts = draw.bindless_ ? nullptr : &draw.descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    if (const VkResult result = vkCreatePipelineLayout(
            info.device, &layoutInfo, nullptr, &draw.pipelineLayout_); result != VK_SUCCESS)
    {
        draw.destroy();
        return std::unexpected(vulkan::describeFailure("vkCreatePipelineLayout", result));
    }

    auto pipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = VK_FORMAT_UNDEFINED,
        .vertexShader = draw.vertexModule_,
        .fragmentShader = draw.fragmentModule_,
        .pipelineLayout = draw.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = false,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_ALWAYS,
        .backfaceCulling = false,
        .viewportWidth = static_cast<float>(info.width),
        .viewportHeight = static_cast<float>(info.height),
    });
    if (!pipeline)
    {
        draw.destroy();
        return std::unexpected(pipeline.error());
    }
    draw.pipeline_.emplace(std::move(*pipeline));

    return draw;
}

GpuDrivenDraw::~GpuDrivenDraw()
{
    destroy();
}

GpuDrivenDraw::GpuDrivenDraw(GpuDrivenDraw&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      width_(other.width_),
      height_(other.height_),
      bindless_(other.bindless_),
      drawIndirectCount_(other.drawIndirectCount_),
      vertexModule_(std::exchange(other.vertexModule_, VK_NULL_HANDLE)),
      fragmentModule_(std::exchange(other.fragmentModule_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::move(other.pipeline_)), // optional move leaves source empty

      descriptorSetLayout_(std::exchange(other.descriptorSetLayout_, VK_NULL_HANDLE)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE)),
      descriptorSet_(std::exchange(other.descriptorSet_, VK_NULL_HANDLE)),
      boundBounds_(std::exchange(other.boundBounds_, VK_NULL_HANDLE)),
      boundIndices_(std::exchange(other.boundIndices_, VK_NULL_HANDLE))
{
}

GpuDrivenDraw& GpuDrivenDraw::operator=(GpuDrivenDraw&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        width_ = other.width_;
        height_ = other.height_;
        bindless_ = other.bindless_;
        drawIndirectCount_ = other.drawIndirectCount_;
        vertexModule_ = std::exchange(other.vertexModule_, VK_NULL_HANDLE);
        fragmentModule_ = std::exchange(other.fragmentModule_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        pipeline_ = std::move(other.pipeline_);
        descriptorSetLayout_ = std::exchange(other.descriptorSetLayout_, VK_NULL_HANDLE);
        descriptorPool_ = std::exchange(other.descriptorPool_, VK_NULL_HANDLE);
        descriptorSet_ = std::exchange(other.descriptorSet_, VK_NULL_HANDLE);
        boundBounds_ = std::exchange(other.boundBounds_, VK_NULL_HANDLE);
        boundIndices_ = std::exchange(other.boundIndices_, VK_NULL_HANDLE);
    }
    return *this;
}

void GpuDrivenDraw::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        pipeline_.reset();
        if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (descriptorSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        if (vertexModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertexModule_, nullptr);
        if (fragmentModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fragmentModule_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    pipeline_.reset();
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    descriptorSet_ = VK_NULL_HANDLE;
    vertexModule_ = VK_NULL_HANDLE;
    fragmentModule_ = VK_NULL_HANDLE;
    boundBounds_ = VK_NULL_HANDLE;
    boundIndices_ = VK_NULL_HANDLE;
}

void GpuDrivenDraw::record(VkCommandBuffer cmd,
                           VkBuffer indirectBuffer,
                           VkBuffer boundsBuffer,
                           VkBuffer visibleIndicesBuffer,
                           const float* viewProj16,
                           VkDeviceAddress boundsAddress,
                           VkDeviceAddress visibleIndicesAddress) const
{
    VkViewport viewport{};
    viewport.y = static_cast<float>(height_);
    viewport.width = static_cast<float>(width_);
    viewport.height = -static_cast<float>(height_);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = {width_, height_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());

    if (!bindless_)
    {
        if (boundBounds_ != boundsBuffer || boundIndices_ != visibleIndicesBuffer)
        {
            VkDescriptorBufferInfo infos[2]{};
            infos[0].buffer = boundsBuffer;
            infos[0].range = VK_WHOLE_SIZE;
            infos[1].buffer = visibleIndicesBuffer;
            infos[1].range = VK_WHOLE_SIZE;
            VkWriteDescriptorSet writes[2]{};
            for (std::uint32_t i = 0; i < 2; ++i)
            {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = descriptorSet_;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo = &infos[i];
            }
            vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
            ++descriptorUpdateCount_;
            boundBounds_ = boundsBuffer;
            boundIndices_ = visibleIndicesBuffer;
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    }

    if (bindless_)
    {
        struct Push
        {
            float viewProj[16];
            VkDeviceAddress bounds;
            VkDeviceAddress indices;
        } push{};
        std::memcpy(push.viewProj, viewProj16, sizeof(push.viewProj));
        push.bounds = boundsAddress;
        push.indices = visibleIndicesAddress;
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
    }
    else
    {
        struct Push
        {
            float viewProj[16];
        } push{};
        std::memcpy(push.viewProj, viewProj16, sizeof(push.viewProj));
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
    }

    if (drawIndirectCount_)
    {
        // instanceCount lives at byte 4 of VkDrawIndirectCommand; the same
        // buffer doubles as the draw-count buffer (one draw).
        vkCmdDrawIndirectCount(cmd, indirectBuffer, 0, indirectBuffer,
                               sizeof(std::uint32_t), 1, 4 * sizeof(std::uint32_t));
        lastDrawCallKind_ = DrawCallKind::DrawIndirectCount;
    }
    else
    {
        // GPU atomically wrote instanceCount; vertexCount stays static at 36.
        vkCmdDrawIndirect(cmd, indirectBuffer, 0, 1, 0);
        lastDrawCallKind_ = DrawCallKind::DrawIndirect;
    }
}

} // namespace starforge::gpu_resources
