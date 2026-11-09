#include "dust_field.hpp"

#include "internal/effect_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in dust.slang (96 bytes). DustFieldParams has
// no flow param; drift speed uses the time base directly.
struct PushConstants
{
    float time = 0.0f;
    float density = 1.0f;
    float scattering = 0.8f;
    float absorption = 0.2f;
    math::Vec4f color{0.8f, 0.7f, 0.5f, 1.0f};
    math::Vec4f right{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4f up{0.0f, 1.0f, 0.0f, 0.0f};
    math::Vec4f forward{0.0f, 0.0f, -1.0f, 0.0f};
    math::Vec4f aspectTan{1.0f, 1.0f, 0.0f, 0.0f};
};

static_assert(sizeof(PushConstants) == 96, "Dust push constants must be 96 bytes");
static_assert(offsetof(PushConstants, aspectTan) == 80, "Dust aspectTan offset must be 80");

[[nodiscard]] PushConstants makePush(const DustFieldRenderContext& context, const DustFieldParams& params)
{
    PushConstants push;
    push.time = params.time;
    push.density = params.density;
    push.scattering = params.scattering;
    push.absorption = params.absorption;
    push.color = params.color;

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

std::expected<DustFieldRenderer, std::string> DustFieldRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("DustFieldRenderer: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("DustFieldRenderer: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("DustFieldRenderer: shaderDirectory is empty");
    }

    DustFieldRenderer renderer;
    renderer.device_ = info.device;

    auto effect = internal::FullscreenEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "dust",
        .pushConstantSize = sizeof(PushConstants),
    });
    if (!effect)
    {
        return std::unexpected(std::move(effect.error()));
    }
    renderer.effect_ = std::move(*effect);

    return renderer;
}

DustFieldRenderer::~DustFieldRenderer()
{
    destroy();
}

DustFieldRenderer::DustFieldRenderer(DustFieldRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

DustFieldRenderer& DustFieldRenderer::operator=(DustFieldRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void DustFieldRenderer::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void DustFieldRenderer::render(VkCommandBuffer cmd, const DustFieldRenderContext& context, const DustFieldParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    const PushConstants push = makePush(context, params);
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
