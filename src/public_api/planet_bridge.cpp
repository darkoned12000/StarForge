#include "planet_bridge.hpp"

#include "environment/planet_renderer.hpp"

#include <utility>

namespace starforge::public_api_bridge
{

namespace
{

starforge::environment::PlanetRenderContext toInternalContext(const RawPlanetContext& value)
{
    starforge::environment::PlanetRenderContext out;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
        {
            out.viewProj[c][r] = value.viewProj.columns[c][r];
            out.invViewProj[c][r] = value.invViewProj.columns[c][r];
        }
    out.cameraPosition = math::Vec3f{value.cameraPosition.x, value.cameraPosition.y, value.cameraPosition.z};
    out.resolution = math::Vec2f{value.resolution.x, value.resolution.y};
    out.time = value.time;
    return out;
}

starforge::environment::PlanetParams toInternalParams(const RawPlanetParams& value)
{
    starforge::environment::PlanetParams out;
    out.center = math::Vec3f{value.center.x, value.center.y, value.center.z};
    out.radius = value.radius;
    out.baseColor = math::Vec4f{value.baseColor.x, value.baseColor.y, value.baseColor.z, value.baseColor.w};
    out.shadowColor =
        math::Vec4f{value.shadowColor.x, value.shadowColor.y, value.shadowColor.z, value.shadowColor.w};
    out.rimColor = math::Vec4f{value.rimColor.x, value.rimColor.y, value.rimColor.z, value.rimColor.w};
    out.sunDirection = math::Vec3f{value.sunDirection.x, value.sunDirection.y, value.sunDirection.z};
    out.fresnelPower = value.fresnelPower;
    out.rimStrength = value.rimStrength;
    out.atmosphereRatio = value.atmosphereRatio;
    out.atmosphereFalloff = value.atmosphereFalloff;
    out.atmosphereColor =
        math::Vec4f{value.atmosphereColor.x, value.atmosphereColor.y, value.atmosphereColor.z,
                    value.atmosphereColor.w};
    out.atmosphereIntensity = value.atmosphereIntensity;
    out.depthBias = value.depthBias;
    out.spin = value.spin;
    return out;
}

} // namespace

struct RawPlanet::Impl
{
    starforge::environment::PlanetRenderer renderer;
};

RawPlanet::~RawPlanet() = default;
RawPlanet::RawPlanet(RawPlanet&&) noexcept = default;
RawPlanet& RawPlanet::operator=(RawPlanet&&) noexcept = default;

std::expected<std::unique_ptr<RawPlanet>, std::string> RawPlanet::create(const CreateInfo& info)
{
    auto renderer = starforge::environment::PlanetRenderer::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
    });
    if (!renderer) return std::unexpected(renderer.error());
    auto result = std::unique_ptr<RawPlanet>(new RawPlanet());
    result->impl_ = std::make_unique<Impl>(Impl{std::move(*renderer)});
    return result;
}

void RawPlanet::render(VkCommandBuffer cmd, const RawPlanetContext& context, const RawPlanetParams& params)
{
    if (!impl_) return;
    impl_->renderer.render(cmd, toInternalContext(context), toInternalParams(params));
}

std::expected<void, std::string> RawPlanet::recreate(const CreateInfo& info)
{
    auto renderer = starforge::environment::PlanetRenderer::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
    });
    if (!renderer) return std::unexpected(renderer.error());
    impl_->renderer = std::move(*renderer);
    return {};
}

} // namespace starforge::public_api_bridge
