#include "nebula_renderer.hpp"

#include "internal/effect_pipeline.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in nebula.slang (96 bytes).
struct PushConstants
{
    float time = 0.0f;
    float density = 1.0f;
    float scattering = 0.5f;
    float absorption = 0.1f;
    math::Vec4f color{0.5f, 0.2f, 0.8f, 1.0f};
    math::Vec4f right{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4f up{0.0f, 1.0f, 0.0f, 0.0f};
    math::Vec4f forward{0.0f, 0.0f, -1.0f, 0.0f};
    math::Vec4f aspectTan{1.0f, 1.0f, 0.0f, 0.0f};
};

static_assert(sizeof(PushConstants) == 96, "Nebula push constants must be 96 bytes");
static_assert(offsetof(PushConstants, aspectTan) == 80, "Nebula aspectTan offset must be 80");

[[nodiscard]] PushConstants makePush(const NebulaRenderContext& context, const NebulaParams& params)
{
    PushConstants push;
    push.time = params.time;
    push.density = params.density;
    push.scattering = params.scattering;
    push.absorption = params.absorption;
    push.color = params.baseColor;

    const math::Mat4f invView = glm::inverse(context.viewProj);
    const math::Vec3f right = glm::normalize(math::Vec3f(invView[0]));
    const math::Vec3f up = glm::normalize(math::Vec3f(invView[1]));
    const math::Vec3f forward = -glm::normalize(math::Vec3f(invView[2]));
    const float projRow1Squared = context.viewProj[0][1] * context.viewProj[0][1] +
                                  context.viewProj[1][1] * context.viewProj[1][1] +
                                  context.viewProj[2][1] * context.viewProj[2][1];
    const float tanHalfFov = projRow1Squared > 0.0f ? 1.0f / std::sqrt(projRow1Squared) : 1.0f;
    const float aspect = context.resolution.x / std::max(context.resolution.y, 1.0f);
    push.right = math::Vec4f{right, 0.0f};
    push.up = math::Vec4f{up, 0.0f};
    push.forward = math::Vec4f{forward, 0.0f};
    push.aspectTan = math::Vec4f{aspect, tanHalfFov, 0.0f, 0.0f};
    return push;
}

} // namespace

std::expected<NebulaRenderer, std::string> NebulaRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("NebulaRenderer: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("NebulaRenderer: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("NebulaRenderer: shaderDirectory is empty");
    }

    NebulaRenderer renderer;
    renderer.device_ = info.device;

    auto effect = internal::FullscreenEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "nebula",
        .pushConstantSize = sizeof(PushConstants),
    });
    if (!effect)
    {
        return std::unexpected(std::move(effect.error()));
    }
    renderer.effect_ = std::move(*effect);

    return renderer;
}

NebulaRenderer::~NebulaRenderer()
{
    destroy();
}

NebulaRenderer::NebulaRenderer(NebulaRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

NebulaRenderer& NebulaRenderer::operator=(NebulaRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void NebulaRenderer::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void NebulaRenderer::render(VkCommandBuffer cmd, const NebulaRenderContext& context, const NebulaParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    const PushConstants push = makePush(context, params);
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
