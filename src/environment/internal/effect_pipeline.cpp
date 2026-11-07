#include "effect_pipeline.hpp"

#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"

#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment::internal
{

namespace
{

std::vector<char> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file)
    {
        return {};
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

std::expected<VkShaderModule, std::string> loadShaderModule(VkDevice device, const std::string& path)
{
    const auto spv = readFile(path);
    if (spv.empty())
    {
        return std::unexpected("Cannot read shader file: " + path);
    }
    constexpr std::size_t kSpirvHeaderBytes = 20;
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;
    if (spv.size() < kSpirvHeaderBytes || (spv.size() % sizeof(std::uint32_t)) != 0)
    {
        return std::unexpected("Corrupt SPIR-V binary: invalid byte count in " + path);
    }
    std::uint32_t magic = 0;
    std::memcpy(&magic, spv.data(), sizeof(magic));
    if (magic != kSpirvMagic)
    {
        return std::unexpected("Corrupt SPIR-V binary: bad magic in " + path);
    }
    std::vector<std::uint32_t> aligned(spv.size() / sizeof(std::uint32_t));
    std::memcpy(aligned.data(), spv.data(), spv.size());
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = aligned.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateShaderModule(device, &info, nullptr, &module);
        result != VK_SUCCESS)
    {
        return std::unexpected(vulkan::describeFailure("vkCreateShaderModule", result));
    }
    return module;
}

struct PipelineBundle
{
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline;
};

std::expected<PipelineBundle, std::string> createBundle(
    const std::string& dir, const std::string& base, VkDevice device, VkFormat colorFormat,
    std::uint32_t pushSize, VkShaderStageFlags stages,
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
{
    PipelineBundle bundle;
    auto vertModule = loadShaderModule(device, dir + "/" + base + ".vert.spv");
    if (!vertModule)
    {
        return std::unexpected(std::move(vertModule.error()));
    }
    auto fragModule = loadShaderModule(device, dir + "/" + base + ".frag.spv");
    if (!fragModule)
    {
        vkDestroyShaderModule(device, *vertModule, nullptr);
        return std::unexpected(std::move(fragModule.error()));
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = stages;
    pushRange.offset = 0;
    pushRange.size = pushSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = pushSize > 0 ? 1 : 0;
    layoutInfo.pPushConstantRanges = pushSize > 0 ? &pushRange : nullptr;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (const VkResult result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);
        result != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, *vertModule, nullptr);
        vkDestroyShaderModule(device, *fragModule, nullptr);
        return std::unexpected(vulkan::describeFailure("vkCreatePipelineLayout", result));
    }
    bundle.layout = layout;

    // Overlay effects blend additively: outputs are premultiplied glow
    // colors, so frames compose (stars/planets show through faint regions).
    auto pipeline = vulkan::GraphicsPipeline::create({
        .device = device,
        .colorFormat = colorFormat,
        .depthFormat = VK_FORMAT_UNDEFINED,
        .vertexShader = *vertModule,
        .fragmentShader = *fragModule,
        .pipelineLayout = bundle.layout,
        .topology = topology,
        .depthTest = false,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_ALWAYS,
        .backfaceCulling = false,
        .blendEnable = true,
        .blendSrcFactor = VK_BLEND_FACTOR_ONE,
        .blendDstFactor = VK_BLEND_FACTOR_ONE,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    vkDestroyShaderModule(device, *vertModule, nullptr);
    vkDestroyShaderModule(device, *fragModule, nullptr);
    if (!pipeline)
    {
        vkDestroyPipelineLayout(device, bundle.layout, nullptr);
        bundle.layout = VK_NULL_HANDLE;
        return std::unexpected(std::move(pipeline.error()));
    }
    bundle.pipeline = std::move(*pipeline);
    return bundle;
}

void destroyBundle(VkDevice& device, VkPipelineLayout& layout, std::optional<vulkan::GraphicsPipeline>& pipeline)
{
    pipeline.reset();
    if (device != VK_NULL_HANDLE && layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, layout, nullptr);
    }
    layout = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
}

} // namespace

// --- FullscreenEffect ---

std::expected<FullscreenEffect, std::string> FullscreenEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("FullscreenEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("FullscreenEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty() || info.shaderBaseName.empty())
    {
        return std::unexpected("FullscreenEffect: shader directory/name is empty");
    }

    FullscreenEffect effect;
    effect.device_ = info.device;
    auto bundle = createBundle(info.shaderDirectory, info.shaderBaseName, info.device,
                               info.colorFormat, info.pushConstantSize,
                               VK_SHADER_STAGE_FRAGMENT_BIT);
    if (!bundle)
    {
        return std::unexpected(std::move(bundle.error()));
    }
    effect.pipelineLayout_ = bundle->layout;
    effect.pipeline_ = std::move(bundle->pipeline);
    return effect;
}

FullscreenEffect::~FullscreenEffect()
{
    destroy();
}

FullscreenEffect::FullscreenEffect(FullscreenEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt))
{
}

FullscreenEffect& FullscreenEffect::operator=(FullscreenEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, std::nullopt);
    }
    return *this;
}

void FullscreenEffect::destroy() noexcept
{
    destroyBundle(device_, pipelineLayout_, pipeline_);
}

void FullscreenEffect::render(VkCommandBuffer cmd, const void* pushData, std::uint32_t pushSize) const
{
    if (!pipeline_.has_value())
    {
        return;
    }
    if (pushData != nullptr && pushSize > 0)
    {
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
    }
    pipeline_->bindAndDraw(cmd, 3, 0, 1);
}

// --- BillboardEffect ---

std::expected<BillboardEffect, std::string> BillboardEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("BillboardEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("BillboardEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty() || info.shaderBaseName.empty())
    {
        return std::unexpected("BillboardEffect: shader directory/name is empty");
    }

    BillboardEffect effect;
    effect.device_ = info.device;
    auto bundle = createBundle(info.shaderDirectory, info.shaderBaseName, info.device,
                               info.colorFormat, info.pushConstantSize,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    if (!bundle)
    {
        return std::unexpected(std::move(bundle.error()));
    }
    effect.pipelineLayout_ = bundle->layout;
    effect.pipeline_ = std::move(bundle->pipeline);
    return effect;
}

BillboardEffect::~BillboardEffect()
{
    destroy();
}

BillboardEffect::BillboardEffect(BillboardEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt))
{
}

BillboardEffect& BillboardEffect::operator=(BillboardEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, std::nullopt);
    }
    return *this;
}

void BillboardEffect::destroy() noexcept
{
    destroyBundle(device_, pipelineLayout_, pipeline_);
}

void BillboardEffect::render(VkCommandBuffer cmd, const void* pushData, std::uint32_t pushSize) const
{
    if (!pipeline_.has_value())
    {
        return;
    }
    if (pushData != nullptr && pushSize > 0)
    {
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, pushSize, pushData);
    }
    // 4-vertex triangle strip quad.
    pipeline_->bindAndDraw(cmd, 4, 0, 1);
}

} // namespace starforge::environment::internal
