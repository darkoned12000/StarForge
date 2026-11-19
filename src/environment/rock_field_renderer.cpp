#include "rock_field_renderer.hpp"

#include "vulkan/vulkan_result.hpp"

#include <cstring>
#include <fstream>
#include <type_traits>
#include <utility>
#include <vector>

namespace starforge::environment
{

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

VkShaderModule loadModule(VkDevice device, const std::string& path)
{
    const auto spv = readFile(path);
    if (spv.size() < 20 || (spv.size() % 4) != 0) return VK_NULL_HANDLE;
    std::vector<std::uint32_t> words(spv.size() / 4);
    std::memcpy(words.data(), spv.data(), spv.size());
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS ? module : VK_NULL_HANDLE;
}

// Must match Push in rock_*.slang (112 bytes).
struct alignas(16) Push
{
    float viewProj[16] = {};
    std::uint64_t vertexAddress = 0;
    std::uint64_t instanceAddress = 0;
    float projectionScale[2] = {1.0f, 1.0f};
    float resolution[2] = {1.0f, 1.0f};
    alignas(16) float sunDirection[3] = {1.0f, 0.5f, 0.3f};
    float pad = 0.0f;
};
static_assert(std::is_standard_layout_v<Push>);
static_assert(sizeof(Push) == 112, "rock field Push must be 112 bytes");
static_assert(offsetof(Push, sunDirection) == 96);

} // namespace

RockFieldRenderer::~RockFieldRenderer() { destroy(); }
RockFieldRenderer::RockFieldRenderer(RockFieldRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      layout_(std::exchange(other.layout_, VK_NULL_HANDLE)),
      mesh_(std::move(other.mesh_)),
      outline_(std::move(other.outline_)),
      impostor_(std::move(other.impostor_))
{
}
RockFieldRenderer& RockFieldRenderer::operator=(RockFieldRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        layout_ = std::exchange(other.layout_, VK_NULL_HANDLE);
        mesh_ = std::move(other.mesh_);
        outline_ = std::move(other.outline_);
        impostor_ = std::move(other.impostor_);
    }
    return *this;
}

std::expected<RockFieldRenderer, std::string> RockFieldRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.shaderDirectory.empty())
        return std::unexpected("RockFieldRenderer requires a device and shader directory");

    RockFieldRenderer renderer;
    renderer.device_ = info.device;
    const std::string dir = info.shaderDirectory;
    const char* bases[3] = {"rock_field", "rock_outline", "rock_impostor"};
    VkShaderModule verts[3] = {};
    VkShaderModule frags[3] = {};
    for (int i = 0; i < 3; ++i)
    {
        verts[i] = loadModule(info.device, dir + "/" + bases[i] + ".vert.spv");
        frags[i] = loadModule(info.device, dir + "/" + bases[i] + ".frag.spv");
        if (!verts[i] || !frags[i])
        {
            for (int j = 0; j < 3; ++j)
            {
                if (verts[j]) vkDestroyShaderModule(info.device, verts[j], nullptr);
                if (frags[j]) vkDestroyShaderModule(info.device, frags[j], nullptr);
            }
            return std::unexpected(std::string("RockFieldRenderer: missing shader ") + bases[i]);
        }
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(Push);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(info.device, &layoutInfo, nullptr, &renderer.layout_) != VK_SUCCESS)
    {
        for (int j = 0; j < 3; ++j)
        {
            vkDestroyShaderModule(info.device, verts[j], nullptr);
            vkDestroyShaderModule(info.device, frags[j], nullptr);
        }
        return std::unexpected("RockFieldRenderer: vkCreatePipelineLayout failed");
    }

    struct Spec
    {
        bool frontCull;
        bool blend;
    };
    const Spec specs[3] = {{false, false}, {true, false}, {false, false}};
    std::optional<vulkan::GraphicsPipeline>* pipelines[3] = {&renderer.mesh_, &renderer.outline_,
                                                             &renderer.impostor_};
    for (int i = 0; i < 3; ++i)
    {
        auto pipeline = vulkan::GraphicsPipeline::create({
            .device = info.device,
            .colorFormat = info.colorFormat,
            .depthFormat = info.depthFormat,
            .vertexShader = verts[i],
            .fragmentShader = frags[i],
            .pipelineLayout = renderer.layout_,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .depthTest = true,
            .depthWrite = true,
            .depthCompare = VK_COMPARE_OP_LESS,
            .backfaceCulling = specs[i].frontCull,
            .cullFrontFace = specs[i].frontCull,
            .viewportWidth = 1.0f,
            .viewportHeight = 1.0f,
        });
        if (!pipeline)
        {
            auto error = pipeline.error();
            for (int j = 0; j < 3; ++j)
            {
                vkDestroyShaderModule(info.device, verts[j], nullptr);
                vkDestroyShaderModule(info.device, frags[j], nullptr);
            }
            renderer.destroy();
            return std::unexpected(error);
        }
        pipelines[i]->emplace(std::move(*pipeline));
    }
    for (int j = 0; j < 3; ++j)
    {
        vkDestroyShaderModule(info.device, verts[j], nullptr);
        vkDestroyShaderModule(info.device, frags[j], nullptr);
    }
    return renderer;
}

void RockFieldRenderer::render(VkCommandBuffer cmd, const RockRenderContext& context,
                               const RockDrawParams& params) const
{
    if (!mesh_ || !outline_ || !impostor_) return;

    Push push;
    std::memcpy(push.viewProj, &context.viewProj, sizeof(push.viewProj));
    push.vertexAddress = params.vertexAddress;
    push.instanceAddress = params.nearInstanceAddress;
    push.projectionScale[0] = context.projectionScale.x;
    push.projectionScale[1] = context.projectionScale.y;
    push.resolution[0] = context.resolution.x;
    push.resolution[1] = context.resolution.y;
    push.sunDirection[0] = context.sunDirection.x;
    push.sunDirection[1] = context.sunDirection.y;
    push.sunDirection[2] = context.sunDirection.z;

    // Near rocks: one instanced mesh draw + one distance-aware outline draw.
    if (params.nearInstanceCount > 0 && params.indexCount > 0)
    {
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        mesh_->bindAndDraw(cmd, 0, params.indexCount, params.nearInstanceCount);
        if (params.outline)
        {
            outline_->bindAndDraw(cmd, 0, params.indexCount, params.nearInstanceCount);
        }
    }

    // Far rocks: one cheap impostor draw (no index buffer).
    if (params.farInstanceCount > 0)
    {
        push.instanceAddress = params.farInstanceAddress;
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impostor_->handle());
        vkCmdDraw(cmd, 6, params.farInstanceCount, 0, 0);
    }
}

void RockFieldRenderer::destroy() noexcept
{
    mesh_.reset();
    outline_.reset();
    impostor_.reset();
    if (device_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    device_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
}

} // namespace starforge::environment
