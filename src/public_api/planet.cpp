#include "starforge/planet.hpp"

#include "convert.hpp"
#include "planet_bridge.hpp"
#include "renderer_impl.hpp"
#include "shader_dir.hpp"

#include <string>
#include <utility>

namespace starforge
{
namespace
{
namespace bridge = public_api_bridge;
using public_api_convert::toRaw;

[[nodiscard]] bridge::RawPlanet::CreateInfo makeBackendInfo(Renderer& renderer,
                                                            const std::string& shaderDirectory)
{
    return bridge::RawPlanet::CreateInfo{
        .device = RendererInternalAccess::device(renderer),
        .colorFormat = RendererInternalAccess::sceneColorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = shaderDirectory,
        .specializationInfo = RendererInternalAccess::specializationInfo(renderer),
    };
}

[[nodiscard]] std::string resolveShaderDirectory(const std::string& requested)
{
    return public_api_detail::resolveShaderDirectory(requested, "planet_sphere.vert.spv");
}
} // namespace

struct Planet::Impl
{
    std::unique_ptr<bridge::RawPlanet> backend;
    std::string shaderDirectory;
};

Planet::Planet(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Planet::~Planet() = default;
Planet::Planet(Planet&&) noexcept = default;
Planet& Planet::operator=(Planet&&) noexcept = default;

std::expected<Planet, std::string> Planet::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
        return std::unexpected("Planet::create failed: Renderer is invalid or moved-from");
    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawPlanet::create(makeBackendInfo(renderer, shaderDirectory));
    if (!backend) return std::unexpected(backend.error());
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    return Planet(std::move(impl));
}

void Planet::record(FrameContext& frameContext, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend) return;
    bridge::RawPlanetContext rawContext;
    rawContext.viewProj = toRaw(context.viewProj);
    rawContext.invViewProj = toRaw(context.invViewProj);
    const Size2D extent = frameContext.extent();
    const math::Vec2f resolution = (context.resolution.x() <= 0.0f || context.resolution.y() <= 0.0f)
                                       ? math::Vec2f{static_cast<float>(extent.width),
                                                     static_cast<float>(extent.height)}
                                       : context.resolution;
    rawContext.cameraPosition = toRaw(context.cameraPosition);
    rawContext.resolution = toRaw(resolution);
    rawContext.time = context.time;

    bridge::RawPlanetParams rawParams;
    rawParams.center = toRaw(params.center);
    rawParams.radius = params.radius;
    rawParams.baseColor = toRaw(params.baseColor);
    rawParams.shadowColor = toRaw(params.shadowColor);
    rawParams.rimColor = toRaw(params.rimColor);
    rawParams.sunDirection = toRaw(params.sunDirection);
    rawParams.fresnelPower = params.fresnelPower;
    rawParams.rimStrength = params.rimStrength;
    rawParams.atmosphereRatio = params.atmosphereRatio;
    rawParams.atmosphereFalloff = params.atmosphereFalloff;
    rawParams.atmosphereColor = toRaw(params.atmosphereColor);
    rawParams.atmosphereIntensity = params.atmosphereIntensity;
    rawParams.depthBias = params.depthBias;
    rawParams.spin = params.spin;

    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), rawContext, rawParams);
}

std::expected<Renderer::FrameResult, std::string>
Planet::render(Renderer& renderer, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend) return std::unexpected("Planet is moved-from");
    return renderer.renderFrame({}, [this, &context, &params](FrameContext& frameContext) {
        record(frameContext, context, params);
    });
}

std::expected<void, std::string> Planet::recreate(Renderer& renderer)
{
    if (!impl_ || !impl_->backend) return std::unexpected("Planet is moved-from");
    if (!RendererInternalAccess::isValid(renderer))
        return std::unexpected("Planet::recreate failed: Renderer is invalid or moved-from");
    renderer.waitIdle();
    return impl_->backend->recreate(makeBackendInfo(renderer, impl_->shaderDirectory));
}

} // namespace starforge
