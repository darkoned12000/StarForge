#include "starfield_renderer.hpp"

#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

// Must match the PushConstants struct in shaders/graphics/starfield.slang
// (std430: time @0, intensity @4, resolution @8, invViewProj @16; 80 bytes).
struct PushConstants
{
    float time = 0.0f;
    float intensity = 1.0f;
    math::Vec2f resolution{1.0f, 1.0f};
    math::Mat4f invViewProj{1.0f};
};

static_assert(sizeof(PushConstants) == 80, "PushConstants size must match Slang std430 layout (80 bytes)");
static_assert(offsetof(PushConstants, resolution) == 8, "PushConstants resolution offset must be 8");
static_assert(offsetof(PushConstants, invViewProj) == 16, "PushConstants invViewProj offset must be 16");

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

    // SPIR-V binaries are word-aligned and start with a 5-word header
    // (magic 0x07230203, version, generator, bound, schema). Reject
    // truncated/corrupt files before copying (avoids a heap overflow
    // when the byte count is not a multiple of sizeof(uint32_t)).
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

std::expected<StarfieldRenderer, std::string> StarfieldRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("StarfieldRenderer: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("StarfieldRenderer: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("StarfieldRenderer: shaderDirectory is empty");
    }

    StarfieldRenderer renderer;
    renderer.device_ = info.device;

    auto vertModule = loadShaderModule(info.device, info.shaderDirectory + "/starfield.vert.spv");
    if (!vertModule)
    {
        return std::unexpected(std::move(vertModule.error()));
    }
    auto fragModule = loadShaderModule(info.device, info.shaderDirectory + "/starfield.frag.spv");
    if (!fragModule)
    {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        return std::unexpected(std::move(fragModule.error()));
    }

    // Push constants are consumed by the fragment stage only (the vertex
    // shader declares none), so restrict the range to FRAGMENT.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = static_cast<std::uint32_t>(sizeof(PushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
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

    auto pipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *vertModule,
        .fragmentShader = *fragModule,
        .pipelineLayout = renderer.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = false,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_ALWAYS,
        .backfaceCulling = false,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });

    // Shader modules are consumed by pipeline creation and can go away now.
    vkDestroyShaderModule(info.device, *vertModule, nullptr);
    vkDestroyShaderModule(info.device, *fragModule, nullptr);

    if (!pipeline)
    {
        vkDestroyPipelineLayout(info.device, renderer.pipelineLayout_, nullptr);
        renderer.pipelineLayout_ = VK_NULL_HANDLE;
        return std::unexpected(std::move(pipeline.error()));
    }
    renderer.pipeline_ = std::move(*pipeline);
    // (optional<GraphicsPipeline> holds the pipeline; reset() replaces it.)

    // Sun billboard pipeline (shared billboard shader).
    auto sun = internal::BillboardEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "billboard",
        .pushConstantSize = sizeof(internal::BillboardPush),
    });
    if (!sun)
    {
        return std::unexpected(std::move(sun.error()));
    }
    renderer.sunEffect_ = std::move(*sun);

    return renderer;
}

StarfieldRenderer::~StarfieldRenderer()
{
    destroy();
}

StarfieldRenderer::StarfieldRenderer(StarfieldRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      sunEffect_(std::exchange(other.sunEffect_, std::nullopt))
{
}

StarfieldRenderer& StarfieldRenderer::operator=(StarfieldRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, std::nullopt);
        sunEffect_ = std::exchange(other.sunEffect_, std::nullopt);
    }
    return *this;
}

void StarfieldRenderer::destroy() noexcept
{
    // The pipeline layout must not be destroyed while a pipeline using it
    // still exists, so release the pipeline first, then the layout.
    pipeline_.reset();
    sunEffect_.reset();
    if (device_ != VK_NULL_HANDLE && pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    pipelineLayout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

void StarfieldRenderer::render(VkCommandBuffer cmd, const StarfieldRenderContext& context, const StarfieldParams& params)
{
    if (!pipeline_.has_value() || pipeline_->handle() == VK_NULL_HANDLE)
    {
        return;
    }

    const PushConstants push{
        .time = params.time != 0.0f ? params.time : context.time,
        .intensity = params.intensity,
        .resolution = context.resolution,
        .invViewProj = context.invViewProj,
    };

    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, static_cast<std::uint32_t>(sizeof(PushConstants)), &push);

    // Fullscreen triangle: 3 vertices, no vertex buffers.
    pipeline_->bindAndDraw(cmd, 3, 0, 1);

    // Sun billboard at the params' world position: frustum-culled when
    // behind the camera plane, aspect-corrected (no widescreen ellipse).
    if (sunEffect_.has_value())
    {
        const math::Vec4f clip = context.viewProj * math::Vec4f{params.worldPosition, 1.0f};
        if (clip.w > 1e-3f)
        {
            const math::Vec3f ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
            const float aspect = context.resolution.x / std::max(context.resolution.y, 1.0f);
            internal::BillboardPush sunPush;
            sunPush.centerSize = {
                ndc.x, ndc.y,
                internal::ndcHalfSize(context.viewProj, params.worldPosition, params.radius), aspect};
            sunPush.color = params.color;
            sunPush.time = context.time;
            sunPush.intensity = params.intensity;
            sunPush.shape = static_cast<std::uint32_t>(internal::BillboardShape::Disc);
            sunEffect_->render(cmd, &sunPush, sizeof(sunPush));
        }
    }
}

} // namespace starforge::environment
