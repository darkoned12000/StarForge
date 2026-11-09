#include "toon_renderer.hpp"

#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in shaders/graphics/toon.slang (192 bytes:
// mvp, modelView, lightDirView, baseColor, shadowColor, params).
struct PushConstants
{
    math::Mat4f mvp{1.0f};
    math::Mat4f modelView{1.0f};
    math::Vec4f lightDirView{0.0f, 0.0f, 1.0f, 0.0f};
    math::Vec4f baseColor{1.0f, 0.25f, 0.2f, 1.0f};
    math::Vec4f shadowColor{0.25f, 0.05f, 0.1f, 1.0f};
    math::Vec4f params{3.0f, 0.5f, 0.02f, 0.0f};
};

static_assert(sizeof(PushConstants) == 192, "Toon push constants must be 192 bytes");
static_assert(offsetof(PushConstants, params) == 176, "Toon params offset must be 176");

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

} // namespace

std::expected<ToonRenderer, std::string> ToonRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("ToonRenderer: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("ToonRenderer: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("ToonRenderer: shaderDirectory is empty");
    }

    VkPhysicalDeviceProperties deviceProps{};
    vkGetPhysicalDeviceProperties(info.physicalDevice, &deviceProps);
    if (deviceProps.limits.maxPushConstantsSize < sizeof(PushConstants))
    {
        return std::unexpected("ToonRenderer: device push-constant range is smaller than 192 bytes");
    }

    ToonRenderer renderer;
    renderer.device_ = info.device;

    auto vertModule = loadShaderModule(info.device, info.shaderDirectory + "/toon.vert.spv");
    if (!vertModule)
    {
        return std::unexpected(vertModule.error());
    }
    auto fragModule = loadShaderModule(info.device, info.shaderDirectory + "/toon.frag.spv");
    if (!fragModule)
    {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        return std::unexpected(fragModule.error());
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = static_cast<std::uint32_t>(sizeof(PushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (const VkResult result = vkCreatePipelineLayout(info.device, &layoutInfo, nullptr, &layout);
        result != VK_SUCCESS)
    {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        vkDestroyShaderModule(info.device, *fragModule, nullptr);
        return std::unexpected(vulkan::describeFailure("vkCreatePipelineLayout", result));
    }
    renderer.pipelineLayout_ = layout;

    auto toon = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *vertModule,
        .fragmentShader = *fragModule,
        .pipelineLayout = renderer.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_LESS,
        .backfaceCulling = true,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    auto outline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *vertModule,
        .fragmentShader = *fragModule,
        .pipelineLayout = renderer.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        // The outline pass runs after the base pass and must not write
        // depth, so expanded back faces cannot poke through recesses.
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_LESS,
        .backfaceCulling = false,
        .cullFrontFace = true,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });

    // Shader modules are consumed by pipeline creation.
    vkDestroyShaderModule(info.device, *vertModule, nullptr);
    vkDestroyShaderModule(info.device, *fragModule, nullptr);

    if (!toon)
    {
        vkDestroyPipelineLayout(info.device, renderer.pipelineLayout_, nullptr);
        renderer.pipelineLayout_ = VK_NULL_HANDLE;
        return std::unexpected(toon.error());
    }
    if (!outline)
    {
        vkDestroyPipelineLayout(info.device, renderer.pipelineLayout_, nullptr);
        renderer.pipelineLayout_ = VK_NULL_HANDLE;
        return std::unexpected(outline.error());
    }
    renderer.toonPipeline_ = std::move(*toon);
    renderer.outlinePipeline_ = std::move(*outline);
    return renderer;
}

ToonRenderer::~ToonRenderer()
{
    destroy();
}

ToonRenderer::ToonRenderer(ToonRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      toonPipeline_(std::exchange(other.toonPipeline_, std::nullopt)),
      outlinePipeline_(std::exchange(other.outlinePipeline_, std::nullopt))
{
}

ToonRenderer& ToonRenderer::operator=(ToonRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        toonPipeline_ = std::exchange(other.toonPipeline_, std::nullopt);
        outlinePipeline_ = std::exchange(other.outlinePipeline_, std::nullopt);
    }
    return *this;
}

void ToonRenderer::destroy() noexcept
{
    outlinePipeline_.reset();
    toonPipeline_.reset();
    if (device_ != VK_NULL_HANDLE && pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    pipelineLayout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

void ToonRenderer::render(VkCommandBuffer cmd, const ToonRenderContext& context, const ToonParams& params)
{
    if (!toonPipeline_.has_value() || !outlinePipeline_.has_value())
    {
        return;
    }

    PushConstants push;
    push.mvp = context.viewProj;
    push.modelView = context.modelView;
    push.lightDirView = math::Vec4f{context.lightDirectionView, 0.0f};
    push.baseColor = params.baseColor;
    push.shadowColor = params.shadowColor;

    // Convert the pixel outline width to object units: view units per
    // pixel at depth d is ~2d / height, and object units scale by the
    // modelView basis length (uniform scale).
    const auto& translation = context.modelView[3];
    const float distance = std::sqrt(translation[0] * translation[0] + translation[1] * translation[1] +
                                     translation[2] * translation[2]);
    const auto& basis = context.modelView[0];
    const float scale = std::sqrt(basis[0] * basis[0] + basis[1] * basis[1] + basis[2] * basis[2]);
    const float height = context.resolution.y > 0.0f ? context.resolution.y : 1.0f;
    const float outlineObjectWidth =
        scale > 0.0f ? params.outlineWidth * distance / height * 2.0f / scale : 0.0f;

    // Base pass first: writes depth, so the outline pass (which runs
    // after without depth writes) only appears beyond the silhouette.
    push.params = math::Vec4f{std::max(params.bandCount, 2.0f), params.rimStrength, outlineObjectWidth, 0.0f};
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    toonPipeline_->bindAndDraw(cmd, 36, 0, 1);

    push.params.w = 1.0f;
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    outlinePipeline_->bindAndDraw(cmd, 36, 0, 1);
}

} // namespace starforge::environment
