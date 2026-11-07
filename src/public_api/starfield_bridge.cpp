#include "starfield_bridge.hpp"

#include "environment/starfield_renderer.hpp"

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

[[nodiscard]] environment::StarfieldRenderContext toInternalContext(const RawStarfieldContext& value) noexcept
{
    environment::StarfieldRenderContext result;
    result.viewProj = toInternalMat4(value.viewProj);
    result.invViewProj = toInternalMat4(value.invViewProj);
    result.cameraPosition = ::starforge::math::Vec3f{value.cameraPosition.x, value.cameraPosition.y, value.cameraPosition.z};
    result.resolution = ::starforge::math::Vec2f{value.resolution.x, value.resolution.y};
    result.time = value.time;
    return result;
}

[[nodiscard]] environment::StarfieldParams toInternalParams(const RawStarfieldParams& value) noexcept
{
    environment::StarfieldParams result;
    result.worldPosition = ::starforge::math::Vec3f{value.worldPosition.x, value.worldPosition.y, value.worldPosition.z};
    result.radius = value.radius;
    result.intensity = value.intensity;
    result.color = ::starforge::math::Vec4f{value.color.x, value.color.y, value.color.z, value.color.w};
    result.limbDarkeningCoeff = value.limbDarkeningCoeff;
    return result;
}

} // namespace

struct RawStarfield::Impl
{
    environment::StarfieldRenderer renderer;
};

RawStarfield::~RawStarfield() = default;
RawStarfield::RawStarfield(RawStarfield&& other) noexcept : impl_(std::move(other.impl_))
{
}
RawStarfield& RawStarfield::operator=(RawStarfield&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<std::unique_ptr<RawStarfield>, std::string> RawStarfield::create(const CreateInfo& info)
{
    auto backend = environment::StarfieldRenderer::create({
        .device = info.device,
        .physicalDevice = info.physicalDevice,
        .queueFamily = info.queueFamily,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
    });
    if (!backend)
    {
        return std::unexpected(backend.error());
    }

    auto result = std::unique_ptr<RawStarfield>(new RawStarfield());
    result->impl_ = std::make_unique<Impl>(Impl{std::move(*backend)});
    return result;
}

void RawStarfield::render(VkCommandBuffer cmd, const RawStarfieldContext& context, const RawStarfieldParams& params)
{
    if (impl_)
    {
        impl_->renderer.render(cmd, toInternalContext(context), toInternalParams(params));
    }
}

} // namespace starforge::public_api_bridge
