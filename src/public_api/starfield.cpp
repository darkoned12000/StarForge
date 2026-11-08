#include "starforge/starfield.hpp"

#include "renderer_impl.hpp"
#include "shader_dir.hpp"
#include "starfield_bridge.hpp"

#include <string>
#include <utility>

namespace starforge
{
namespace
{
namespace bridge = public_api_bridge;

[[nodiscard]] bridge::RawMat4f toRaw(const math::Mat4f& value) noexcept
{
    bridge::RawMat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result.columns[column][row] = value.columns[column][row];
        }
    }
    return result;
}

[[nodiscard]] bridge::RawVec3f toRaw(const math::Vec3f& value) noexcept
{
    return bridge::RawVec3f{value.x(), value.y(), value.z()};
}

[[nodiscard]] bridge::RawVec2f toRaw(const math::Vec2f& value) noexcept
{
    return bridge::RawVec2f{value.x(), value.y()};
}

[[nodiscard]] bridge::RawVec4f toRaw(const math::Vec4f& value) noexcept
{
    return bridge::RawVec4f{value.x(), value.y(), value.z(), value.w()};
}

[[nodiscard]] bridge::RawStarfield::CreateInfo makeBackendInfo(Renderer& renderer, const std::string& shaderDirectory)
{
    return bridge::RawStarfield::CreateInfo{
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .queueFamily = RendererInternalAccess::queueFamily(renderer),
        .colorFormat = RendererInternalAccess::colorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = shaderDirectory,
    };
}

[[nodiscard]] std::string resolveShaderDirectory(const std::string& requested)
{
    return public_api_detail::resolveShaderDirectory(requested, "starfield.vert.spv");
}

} // namespace

struct Starfield::Impl
{
    std::unique_ptr<bridge::RawStarfield> backend;
    std::string shaderDirectory;
};

Starfield::Starfield(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

Starfield::~Starfield() = default;

Starfield::Starfield(Starfield&& other) noexcept : impl_(std::move(other.impl_))
{
}

Starfield& Starfield::operator=(Starfield&& other) noexcept
{
    impl_ = std::move(other.impl_);
    return *this;
}

std::expected<Starfield, std::string> Starfield::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Starfield::create failed: Renderer is invalid or moved-from");
    }

    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawStarfield::create(makeBackendInfo(renderer, shaderDirectory));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }

    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    return Starfield(std::move(impl));
}

void Starfield::record(FrameContext& frameContext, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return;
    }

    bridge::RawStarfieldContext rawContext;
    rawContext.viewProj = toRaw(context.viewProj);
    rawContext.invViewProj = toRaw(context.invViewProj);
    rawContext.cameraPosition = toRaw(context.cameraPosition);

    const Size2D extent = frameContext.extent();
    const math::Vec2f resolution = (context.resolution.x() <= 0.0f || context.resolution.y() <= 0.0f)
                                       ? math::Vec2f{static_cast<float>(extent.width), static_cast<float>(extent.height)}
                                       : context.resolution;
    rawContext.resolution = toRaw(resolution);
    rawContext.time = context.time;

    bridge::RawStarfieldParams rawParams;
    rawParams.worldPosition = toRaw(params.worldPosition);
    rawParams.radius = params.radius;
    rawParams.intensity = params.intensity;
    rawParams.color = toRaw(params.color);
    rawParams.limbDarkeningCoeff = params.limbDarkeningCoeff;

    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), rawContext, rawParams);
}

std::expected<Renderer::FrameResult, std::string>
Starfield::render(Renderer& renderer, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return std::unexpected("Starfield is moved-from");
    }

    return renderer.renderFrame(context.clearColor, [this, &context, &params](FrameContext& frameContext) {
        record(frameContext, context, params);
    });
}

std::expected<void, std::string> Starfield::recreate(Renderer& renderer)
{
    if (!impl_)
    {
        return std::unexpected("Starfield is moved-from");
    }
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Starfield::recreate failed: Renderer is invalid or moved-from");
    }

    renderer.waitIdle();
    auto backend = bridge::RawStarfield::create(makeBackendInfo(renderer, impl_->shaderDirectory));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }
    impl_->backend = std::move(*backend);
    return {};
}

} // namespace starforge
