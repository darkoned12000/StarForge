#include "nebula_renderer.hpp"

#include "internal/effect_pipeline.hpp"

#include <cstddef>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in nebula.slang (104 bytes).
struct PushConstants
{
    float time = 0.0f;
    float density = 1.0f;
    float scattering = 0.5f;
    float absorption = 0.1f;
    math::Vec4f color{0.5f, 0.2f, 0.8f, 1.0f};
    math::Vec2f resolution{1.0f, 1.0f};
    alignas(16) math::Mat4f invViewProj{1.0f};
};

static_assert(sizeof(PushConstants) == 112, "Nebula push constants must be 112 bytes");
static_assert(offsetof(PushConstants, resolution) == 32, "Nebula resolution offset must be 32");
static_assert(offsetof(PushConstants, invViewProj) == 48, "Nebula invViewProj offset must be 48");

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
    const PushConstants push{
        .time = params.time,
        .density = params.density,
        .scattering = params.scattering,
        .absorption = params.absorption,
        .color = params.baseColor,
        .resolution = context.resolution,
        .invViewProj = context.invViewProj,
    };
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
