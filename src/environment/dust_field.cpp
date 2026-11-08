#include "dust_field.hpp"

#include "internal/effect_pipeline.hpp"

#include <cstddef>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in dust.slang (104 bytes). DustFieldParams has
// no flow param; drift speed uses the time base directly.
struct PushConstants
{
    float time = 0.0f;
    float density = 1.0f;
    float scattering = 0.8f;
    float absorption = 0.2f;
    math::Vec4f color{0.8f, 0.7f, 0.5f, 1.0f};
    math::Vec2f resolution{1.0f, 1.0f};
    alignas(16) math::Mat4f invViewProj{1.0f};
};

static_assert(sizeof(PushConstants) == 112, "Dust push constants must be 112 bytes");
static_assert(offsetof(PushConstants, invViewProj) == 48, "Dust invViewProj offset must be 48");

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
    const PushConstants push{
        .time = params.time,
        .density = params.density,
        .scattering = params.scattering,
        .absorption = params.absorption,
        .color = params.color,
        .resolution = context.resolution,
        .invViewProj = context.invViewProj,
    };
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
