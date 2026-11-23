#include "combat_fx_renderer.hpp"

#include "internal/effect_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"

#include <cstring>
#include <type_traits>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match Push in combat_fx.slang.
struct alignas(16) Push
{
    float viewProj[16] = {};
    float cameraRight[4] = {};
    float cameraUp[4] = {};
    float viewport[4] = {};
    std::uint64_t instanceAddress = 0;
};
static_assert(std::is_standard_layout_v<Push>);
static_assert(sizeof(Push) == 128, "combat_fx Push must fit the 128-byte minimum");
static_assert(offsetof(Push, cameraRight) == 64);
static_assert(offsetof(Push, cameraUp) == 80);
static_assert(offsetof(Push, viewport) == 96);
static_assert(offsetof(Push, instanceAddress) == 112);

} // namespace

CombatFxRenderer::~CombatFxRenderer() { destroy(); }

CombatFxRenderer::CombatFxRenderer(CombatFxRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      layout_(std::exchange(other.layout_, VK_NULL_HANDLE)),
      pipeline_(std::move(other.pipeline_))
{
}

CombatFxRenderer& CombatFxRenderer::operator=(CombatFxRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        layout_ = std::exchange(other.layout_, VK_NULL_HANDLE);
        pipeline_ = std::move(other.pipeline_);
    }
    return *this;
}

std::expected<CombatFxRenderer, std::string>
CombatFxRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.shaderDirectory.empty())
        return std::unexpected("CombatFxRenderer requires a device and shader directory");

    CombatFxRenderer renderer;
    renderer.device_ = info.device;

    auto vert = internal::loadShaderModule(info.device, info.shaderDirectory + "/combat_fx.vert.spv");
    if (!vert) return std::unexpected(vert.error());
    auto frag = internal::loadShaderModule(info.device, info.shaderDirectory + "/combat_fx.frag.spv");
    if (!frag)
    {
        vkDestroyShaderModule(info.device, *vert, nullptr);
        return std::unexpected(frag.error());
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Push);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(info.device, &layoutInfo, nullptr, &renderer.layout_) != VK_SUCCESS)
    {
        vkDestroyShaderModule(info.device, *vert, nullptr);
        vkDestroyShaderModule(info.device, *frag, nullptr);
        return std::unexpected("CombatFxRenderer: vkCreatePipelineLayout failed");
    }

    auto pipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *vert,
        .fragmentShader = *frag,
        .pipelineLayout = renderer.layout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        // Effects are additive and read scene depth but never write it, so
        // they never punch holes in the opaque pass or need sorting.
        .depthTest = true,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_LESS,
        .backfaceCulling = false,
        .blendEnable = true,
        .blendSrcFactor = VK_BLEND_FACTOR_ONE,
        .blendDstFactor = VK_BLEND_FACTOR_ONE,
        .specializationInfo = info.specializationInfo,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    vkDestroyShaderModule(info.device, *vert, nullptr);
    vkDestroyShaderModule(info.device, *frag, nullptr);
    if (!pipeline)
    {
        auto error = pipeline.error();
        renderer.destroy();
        return std::unexpected(error);
    }
    renderer.pipeline_.emplace(std::move(*pipeline));
    return renderer;
}

void CombatFxRenderer::render(VkCommandBuffer cmd, const CombatFxRenderContext& context,
                              const CombatFxDrawParams& params) const
{
    if (!pipeline_ || params.instanceCount == 0 || params.instanceAddress == 0) return;

    Push push{};
    std::memcpy(push.viewProj, &context.viewProj, sizeof(push.viewProj));
    push.cameraRight[0] = context.cameraRight.x;
    push.cameraRight[1] = context.cameraRight.y;
    push.cameraRight[2] = context.cameraRight.z;
    push.cameraUp[0] = context.cameraUp.x;
    push.cameraUp[1] = context.cameraUp.y;
    push.cameraUp[2] = context.cameraUp.z;
    push.viewport[0] = context.resolution.x;
    push.viewport[1] = context.resolution.y;
    // Pack camera position into the spare float slots: viewport.zw = xy,
    // cameraRight.w = z. Keeps Push at 128 bytes (Vulkan's guaranteed min).
    push.viewport[2] = context.cameraPosition.x;
    push.viewport[3] = context.cameraPosition.y;
    push.cameraRight[3] = context.cameraPosition.z;
    push.instanceAddress = params.instanceAddress;

    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    pipeline_->bindAndDraw(cmd, 6, 0, params.instanceCount);
}

void CombatFxRenderer::destroy() noexcept
{
    pipeline_.reset();
    if (device_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    device_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
}

} // namespace starforge::environment
