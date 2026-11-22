#include "planet_renderer.hpp"

#include "internal/effect_pipeline.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

struct SpherePush
{
    std::array<float, 16> viewProj{};
    std::array<float, 16> invViewProj{};
    std::array<float, 4> cameraPos{};
    std::array<float, 4> planet{};
    std::array<float, 4> sunDir{};
    std::array<float, 4> baseColor{};
    std::array<float, 4> shadowColor{};
    std::array<float, 4> rimColor{};
    std::array<float, 4> params{};
};
static_assert(sizeof(SpherePush) == 240, "SpherePush must match planet_sphere.slang");

struct AtmoPush
{
    std::array<float, 16> viewProj{};
    std::array<float, 16> invViewProj{};
    std::array<float, 4> cameraPos{};
    std::array<float, 4> planet{};
    std::array<float, 4> atmoColor{};
    std::array<float, 4> params{};
};
static_assert(sizeof(AtmoPush) == 192, "AtmoPush must match planet_atmosphere.slang");

template <std::size_t N>
void writeMat(std::array<float, N>& out, const math::Mat4f& value)
{
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            out[c * 4 + r] = value[c][r];
}



} // namespace

PlanetRenderer::~PlanetRenderer() { destroy(); }
PlanetRenderer::PlanetRenderer(PlanetRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      sphereVert_(std::exchange(other.sphereVert_, VK_NULL_HANDLE)),
      sphereFrag_(std::exchange(other.sphereFrag_, VK_NULL_HANDLE)),
      atmoVert_(std::exchange(other.atmoVert_, VK_NULL_HANDLE)),
      atmoFrag_(std::exchange(other.atmoFrag_, VK_NULL_HANDLE)),
      sphereLayout_(std::exchange(other.sphereLayout_, VK_NULL_HANDLE)),
      atmoLayout_(std::exchange(other.atmoLayout_, VK_NULL_HANDLE)),
      spherePipeline_(std::move(other.spherePipeline_)),
      atmoPipeline_(std::move(other.atmoPipeline_))
{
}
PlanetRenderer& PlanetRenderer::operator=(PlanetRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        sphereVert_ = std::exchange(other.sphereVert_, VK_NULL_HANDLE);
        sphereFrag_ = std::exchange(other.sphereFrag_, VK_NULL_HANDLE);
        atmoVert_ = std::exchange(other.atmoVert_, VK_NULL_HANDLE);
        atmoFrag_ = std::exchange(other.atmoFrag_, VK_NULL_HANDLE);
        sphereLayout_ = std::exchange(other.sphereLayout_, VK_NULL_HANDLE);
        atmoLayout_ = std::exchange(other.atmoLayout_, VK_NULL_HANDLE);
        spherePipeline_ = std::move(other.spherePipeline_);
        atmoPipeline_ = std::move(other.atmoPipeline_);
    }
    return *this;
}

std::expected<PlanetRenderer, std::string> PlanetRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
        return std::unexpected("PlanetRenderer requires a device");

    const std::string dir = info.shaderDirectory;
    PlanetRenderer renderer;
    renderer.device_ = info.device;
    auto sphereVert = internal::loadShaderModule(info.device, dir + "/planet_sphere.vert.spv");
    auto sphereFrag = internal::loadShaderModule(info.device, dir + "/planet_sphere.frag.spv");
    auto atmoVert = internal::loadShaderModule(info.device, dir + "/planet_atmosphere.vert.spv");
    auto atmoFrag = internal::loadShaderModule(info.device, dir + "/planet_atmosphere.frag.spv");
    if (!sphereVert || !sphereFrag || !atmoVert || !atmoFrag)
    {
        renderer.destroy();
        const auto& err = !sphereVert ? sphereVert.error() : !sphereFrag ? sphereFrag.error()
                          : !atmoVert   ? atmoVert.error()
                                        : atmoFrag.error();
        return std::unexpected(err);
    }
    renderer.sphereVert_ = *sphereVert;
    renderer.sphereFrag_ = *sphereFrag;
    renderer.atmoVert_ = *atmoVert;
    renderer.atmoFrag_ = *atmoFrag;

    auto sphereLayout = vulkan::createPushConstantPipelineLayout(info.device, sizeof(SpherePush));
    if (!sphereLayout)
    {
        renderer.destroy();
        return std::unexpected(sphereLayout.error());
    }
    renderer.sphereLayout_ = *sphereLayout;

    auto atmoLayout = vulkan::createPushConstantPipelineLayout(info.device, sizeof(AtmoPush));
    if (!atmoLayout)
    {
        renderer.destroy();
        return std::unexpected(atmoLayout.error());
    }
    renderer.atmoLayout_ = *atmoLayout;

    auto spherePipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = renderer.sphereVert_,
        .fragmentShader = renderer.sphereFrag_,
        .pipelineLayout = renderer.sphereLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        // The planet is the farthest object in the scene. Draw it without
        // depth test/write right after the nebula so it is always a solid
        // disc; nearer objects (sun, rocks, ships) are drawn afterwards and
        // cover it via their own depth-tested passes. Writing projective
        // depth here dropped the disc at some view angles (only the rim of
        // the atmosphere shell survived), so depth is intentionally unused.
        .depthTest = false,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_ALWAYS,
        .backfaceCulling = false,
        .specializationInfo = info.specializationInfo,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    if (!spherePipeline)
    {
        auto error = spherePipeline.error();
        renderer.destroy();
        return std::unexpected(error);
    }

    auto atmoPipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = renderer.atmoVert_,
        .fragmentShader = renderer.atmoFrag_,
        .pipelineLayout = renderer.atmoLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
        .backfaceCulling = false,
        .blendEnable = true,
        .blendSrcFactor = VK_BLEND_FACTOR_ONE,
        .blendDstFactor = VK_BLEND_FACTOR_ONE,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    if (!atmoPipeline)
    {
        auto error = atmoPipeline.error();
        renderer.destroy();
        return std::unexpected(error);
    }

    renderer.spherePipeline_.emplace(std::move(*spherePipeline));
    renderer.atmoPipeline_.emplace(std::move(*atmoPipeline));
    return renderer;
}

void PlanetRenderer::render(VkCommandBuffer cmd, const PlanetRenderContext& context,
                            const PlanetParams& params) const
{
    if (!spherePipeline_ || !atmoPipeline_) return;

    const float effectiveRadius = params.radius * std::max(1.0f, params.atmosphereRatio);
    const math::Vec4f clip = context.viewProj * math::Vec4f{params.center, 1.0f};
    if (clip.w > 1e-4f)
    {
        const math::Vec3f ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
        const float ndcRadius = internal::ndcHalfSize(context.viewProj, params.center, effectiveRadius);
        if (ndc.x + ndcRadius < -1.0f || ndc.x - ndcRadius > 1.0f ||
            ndc.y + ndcRadius < -1.0f || ndc.y - ndcRadius > 1.0f)
        {
            return;
        }
    }

    SpherePush sphere;
    writeMat(sphere.viewProj, context.viewProj);
    writeMat(sphere.invViewProj, context.invViewProj);
    const auto cp = context.cameraPosition;
    sphere.cameraPos = {cp.x, cp.y, cp.z, 0.0f};
    const auto ctr = params.center;
    sphere.planet = {ctr.x, ctr.y, ctr.z, params.radius};
    const auto sd = params.sunDirection;
    sphere.sunDir = {sd.x, sd.y, sd.z, 0.0f};
    const auto bc = params.baseColor, sc = params.shadowColor, rc = params.rimColor;
    sphere.baseColor = {bc.x, bc.y, bc.z, bc.w};
    sphere.shadowColor = {sc.x, sc.y, sc.z, sc.w};
    sphere.rimColor = {rc.x, rc.y, rc.z, rc.w};
    sphere.params = {params.fresnelPower, params.rimStrength, params.spin, 0.0f};

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, spherePipeline_->handle());
    vkCmdPushConstants(cmd, sphereLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(sphere), &sphere);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // Callers disable the additive limb by setting intensity to zero. Skip
    // the draw entirely: on an HDR target its zero-valued additive output
    // was leaving a dark annulus around the disc.
    if (params.atmosphereIntensity <= 0.0f) return;

    AtmoPush atmo;
    writeMat(atmo.viewProj, context.viewProj);
    writeMat(atmo.invViewProj, context.invViewProj);
    atmo.cameraPos = {cp.x, cp.y, cp.z, 0.0f};
    atmo.planet = {ctr.x, ctr.y, ctr.z, params.radius};
    const auto ac = params.atmosphereColor;
    atmo.atmoColor = {ac.x, ac.y, ac.z, params.atmosphereIntensity};
    atmo.params = {params.atmosphereRatio, params.atmosphereFalloff, params.depthBias, 0.0f};

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, atmoPipeline_->handle());
    vkCmdPushConstants(cmd, atmoLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(atmo), &atmo);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void PlanetRenderer::destroy() noexcept
{
    spherePipeline_.reset();
    atmoPipeline_.reset();
    if (device_ != VK_NULL_HANDLE)
    {
        if (sphereLayout_) vkDestroyPipelineLayout(device_, sphereLayout_, nullptr);
        if (atmoLayout_) vkDestroyPipelineLayout(device_, atmoLayout_, nullptr);
        if (sphereVert_) vkDestroyShaderModule(device_, sphereVert_, nullptr);
        if (sphereFrag_) vkDestroyShaderModule(device_, sphereFrag_, nullptr);
        if (atmoVert_) vkDestroyShaderModule(device_, atmoVert_, nullptr);
        if (atmoFrag_) vkDestroyShaderModule(device_, atmoFrag_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    sphereLayout_ = VK_NULL_HANDLE;
    atmoLayout_ = VK_NULL_HANDLE;
    sphereVert_ = VK_NULL_HANDLE;
    sphereFrag_ = VK_NULL_HANDLE;
    atmoVert_ = VK_NULL_HANDLE;
    atmoFrag_ = VK_NULL_HANDLE;
}

} // namespace starforge::environment
