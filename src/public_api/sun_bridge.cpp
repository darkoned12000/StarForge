#include "sun_bridge.hpp"

#include "environment/sun_renderer.hpp"
#include "spatial/world_position.hpp"

#include <utility>

namespace starforge::public_api_bridge
{
namespace
{

[[nodiscard]] ::starforge::math::Mat4f toInternalMat4(const RawMat4f& value) noexcept
{
    ::starforge::math::Mat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result[static_cast<int>(column)][static_cast<int>(row)] = value.columns[column][row];
        }
    }
    return result;
}

[[nodiscard]] spatial::WorldPosition toInternal(const RawWorldPosition& value) noexcept
{
    spatial::WorldPosition result;
    result.cellX = value.cellX;
    result.cellY = value.cellY;
    result.cellZ = value.cellZ;
    result.offsetX = value.offsetX;
    result.offsetY = value.offsetY;
    result.offsetZ = value.offsetZ;
    return result;
}

[[nodiscard]] environment::SunRenderContext toInternalContext(const RawSunContext& value) noexcept
{
    environment::SunRenderContext result;
    result.viewProj = toInternalMat4(value.viewProj);
    result.cameraPosition = toInternal(value.cameraPosition);
    result.resolution = ::starforge::math::Vec2f{value.resolution.x, value.resolution.y};
    result.time = value.time;
    return result;
}

[[nodiscard]] environment::SunParams toInternalParams(const RawSunParams& value) noexcept
{
    environment::SunParams result;
    result.worldPosition = toInternal(value.worldPosition);
    result.radius = value.radius;
    result.coronaRadiusMultiplier = value.coronaRadiusMultiplier;
    result.coreColor = ::starforge::math::Vec4f{value.coreColor.x, value.coreColor.y, value.coreColor.z, value.coreColor.w};
    result.limbColor = ::starforge::math::Vec4f{value.limbColor.x, value.limbColor.y, value.limbColor.z, value.limbColor.w};
    result.coronaColor = ::starforge::math::Vec4f{value.coronaColor.x, value.coronaColor.y, value.coronaColor.z, value.coronaColor.w};
    result.limbDarkeningCoeff = value.limbDarkeningCoeff;
    result.coronaIntensity = value.coronaIntensity;
    result.time = value.time;
    return result;
}

} // namespace

struct RawSun::Impl
{
    environment::SunRenderer renderer;
};

RawSun::RawSun() = default;
RawSun::~RawSun() = default;
RawSun::RawSun(RawSun&& other) noexcept : impl_(std::move(other.impl_)) {}
RawSun& RawSun::operator=(RawSun&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<std::unique_ptr<RawSun>, std::string> RawSun::create(const CreateInfo& info)
{
    auto renderer = environment::SunRenderer::create({
        .instance = info.instance,
        .device = info.device,
        .physicalDevice = info.physicalDevice,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
        .defaultRadius = info.defaultRadius,
        .subdivisions = info.subdivisions,
        .specializationInfo = info.specializationInfo,
    });
    if (!renderer)
    {
        return std::unexpected(std::move(renderer.error()));
    }

    auto raw = std::unique_ptr<RawSun>(new RawSun());
    raw->impl_ = std::make_unique<Impl>(Impl{.renderer = std::move(*renderer)});
    return raw;
}

void RawSun::render(VkCommandBuffer cmd, const RawSunContext& context, const RawSunParams& params)
{
    if (impl_)
    {
        impl_->renderer.render(cmd, toInternalContext(context), toInternalParams(params));
    }
}

std::expected<void, std::string> RawSun::recreate(const CreateInfo& info)
{
    auto renderer = environment::SunRenderer::create({
        .instance = info.instance,
        .device = info.device,
        .physicalDevice = info.physicalDevice,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
        .defaultRadius = info.defaultRadius,
        .subdivisions = info.subdivisions,
        .specializationInfo = info.specializationInfo,
    });
    if (!renderer)
    {
        return std::unexpected(std::move(renderer.error()));
    }
    if (impl_)
    {
        impl_->renderer = std::move(*renderer);
    }
    return {};
}

} // namespace starforge::public_api_bridge
