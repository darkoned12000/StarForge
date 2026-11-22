#include "rock_field_renderer.hpp"

#include "internal/effect_pipeline.hpp"

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
        auto vert = internal::loadShaderModule(info.device, dir + "/" + bases[i] + ".vert.spv");
        auto frag = internal::loadShaderModule(info.device, dir + "/" + bases[i] + ".frag.spv");
        if (!vert || !frag)
        {
            for (int j = 0; j < 3; ++j)
            {
                if (verts[j]) vkDestroyShaderModule(info.device, verts[j], nullptr);
                if (frags[j]) vkDestroyShaderModule(info.device, frags[j], nullptr);
            }
            return std::unexpected(vert ? frag.error() : vert.error());
        }
        verts[i] = *vert;
        frags[i] = *frag;
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
        bool hdrOutput; // true only for rock_field.slang, which includes output_shoulder.slang
        bool depthWrite;
    };
    // The impostor tier is alpha blended: a distant rock fades up out of the
    // background and solidifies as it comes into focus. It therefore must
    // not write depth (it would punch holes in the opaque pass), and the
    // blend is what makes the outer edge of the field dissolve smoothly
    // instead of popping.
    const Spec specs[3] = {{false, false, true, true},
                            {true, false, false, true},
                            {false, true, false, false}};
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
            .depthWrite = specs[i].depthWrite,
            .depthCompare = VK_COMPARE_OP_LESS,
            .backfaceCulling = specs[i].frontCull,
            .cullFrontFace = specs[i].frontCull,
            .blendEnable = specs[i].blend,
            .blendSrcFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .blendDstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .specializationInfo = specs[i].hdrOutput ? info.specializationInfo : nullptr,
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
        // The outline is an inverted hull: it is drawn FIRST so the solid
        // rock covers it everywhere except the sliver that sticks out past
        // the silhouette. Drawn afterwards it would paint over the crater
        // floors - the hull is built from the smooth radial direction, so
        // inside a concave crater its back faces sit in front of the rock
        // and turn every pit into a black blob.
        if (params.outline)
        {
            outline_->bindAndDraw(cmd, 0, params.indexCount, params.nearInstanceCount);
        }
        mesh_->bindAndDraw(cmd, 0, params.indexCount, params.nearInstanceCount);
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
