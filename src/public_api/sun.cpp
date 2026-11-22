#include "starforge/sun.hpp"

#include "convert.hpp"
#include "renderer_impl.hpp"
#include "shader_dir.hpp"
#include "sun_bridge.hpp"

#include <string>
#include <utility>

namespace starforge
{
namespace
{
namespace bridge = public_api_bridge;
using public_api_convert::toRaw;

[[nodiscard]] bridge::RawSun::CreateInfo makeBackendInfo(Renderer& renderer, const std::string& shaderDirectory,
                                                        double defaultRadius, std::uint32_t subdivisions)
{
    return bridge::RawSun::CreateInfo{
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .queueFamily = RendererInternalAccess::queueFamily(renderer),
        .colorFormat = RendererInternalAccess::sceneColorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = shaderDirectory,
        .defaultRadius = defaultRadius,
        .subdivisions = subdivisions,
        .specializationInfo = RendererInternalAccess::specializationInfo(renderer),
    };
}

[[nodiscard]] std::string resolveShaderDirectory(const std::string& requested)
{
    return public_api_detail::resolveShaderDirectory(requested, "sun.vert.spv");
}

} // namespace

struct Sun::Impl
{
    std::unique_ptr<bridge::RawSun> backend;
    std::string shaderDirectory;
    double defaultRadius = 10000.0;
    std::uint32_t subdivisions = 16;
};

Sun::Sun(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Sun::~Sun() = default;
Sun::Sun(Sun&& other) noexcept : impl_(std::move(other.impl_)) {}
Sun& Sun::operator=(Sun&& other) noexcept
{
    impl_ = std::move(other.impl_);
    return *this;
}

std::expected<Sun, std::string> Sun::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Sun::create failed: Renderer is invalid or moved-from");
    }
    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawSun::create(makeBackendInfo(renderer, shaderDirectory, info.defaultRadius, info.subdivisions));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    impl->defaultRadius = info.defaultRadius;
    impl->subdivisions = info.subdivisions;
    return Sun(std::move(impl));
}

void Sun::record(FrameContext& frameContext, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return;
    }
    bridge::RawSunContext rawContext;
    rawContext.viewProj = toRaw(context.viewProj);
    rawContext.cameraPosition = toRaw(context.cameraPosition);
    const Size2D extent = frameContext.extent();
    const math::Vec2f resolution = (context.resolution.x() <= 0.0f || context.resolution.y() <= 0.0f)
                                       ? math::Vec2f{static_cast<float>(extent.width), static_cast<float>(extent.height)}
                                       : context.resolution;
    rawContext.resolution = toRaw(resolution);
    rawContext.time = context.time;

    bridge::RawSunParams rawParams;
    rawParams.worldPosition = toRaw(params.worldPosition);
    rawParams.radius = params.radius;
    rawParams.coronaRadiusMultiplier = params.coronaRadiusMultiplier;
    rawParams.coreColor = toRaw(params.coreColor);
    rawParams.limbColor = toRaw(params.limbColor);
    rawParams.coronaColor = toRaw(params.coronaColor);
    rawParams.limbDarkeningCoeff = params.limbDarkeningCoeff;
    rawParams.coronaIntensity = params.coronaIntensity;
    rawParams.time = params.time;

    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), rawContext, rawParams);
}

std::expected<Renderer::FrameResult, std::string>
Sun::render(Renderer& renderer, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return std::unexpected("Sun is moved-from");
    }
    return renderer.renderFrame({}, [this, &context, &params](FrameContext& frameContext) {
        record(frameContext, context, params);
    });
}

std::expected<void, std::string> Sun::recreate(Renderer& renderer)
{
    if (!impl_)
    {
        return std::unexpected("Sun is moved-from");
    }
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Sun::recreate failed: Renderer is invalid or moved-from");
    }
    renderer.waitIdle();
    auto backend = bridge::RawSun::create(makeBackendInfo(renderer, impl_->shaderDirectory, impl_->defaultRadius, impl_->subdivisions));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }
    impl_->backend = std::move(*backend);
    return {};
}

} // namespace starforge
