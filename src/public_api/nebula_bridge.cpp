#include "nebula_bridge.hpp"

#include "environment/nebula_renderer.hpp"

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

[[nodiscard]] environment::NebulaRenderContext toInternalContext(const RawNebulaContext& value) noexcept
{
    environment::NebulaRenderContext result;
    result.viewProj = toInternalMat4(value.viewProj);
    result.invViewProj = toInternalMat4(value.invViewProj);
    result.cameraPosition = ::starforge::math::Vec3f{value.cameraPosition.x, value.cameraPosition.y,
                                                     value.cameraPosition.z};
    result.resolution = ::starforge::math::Vec2f{value.resolution.x, value.resolution.y};
    return result;
}

[[nodiscard]] environment::NebulaParams toInternalParams(const RawNebulaParams& value) noexcept
{
    environment::NebulaParams result;
    result.baseColor = ::starforge::math::Vec4f{value.baseColor.x, value.baseColor.y, value.baseColor.z,
                                                 value.baseColor.w};
    result.density = value.density;
    result.absorption = value.absorption;
    result.scattering = value.scattering;
    result.time = value.time;
    return result;
}

} // namespace

struct RawNebula::Impl
{
    environment::NebulaRenderer renderer;
};

RawNebula::~RawNebula() = default;
RawNebula::RawNebula(RawNebula&& other) noexcept : impl_(std::move(other.impl_)) {}
RawNebula& RawNebula::operator=(RawNebula&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<std::unique_ptr<RawNebula>, std::string> RawNebula::create(const CreateInfo& info)
{
    auto backend = environment::NebulaRenderer::create({
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
    auto result = std::unique_ptr<RawNebula>(new RawNebula());
    result->impl_ = std::make_unique<Impl>(Impl{std::move(*backend)});
    return result;
}

void RawNebula::render(VkCommandBuffer cmd, const RawNebulaContext& context, const RawNebulaParams& params)
{
    if (impl_)
    {
        impl_->renderer.render(cmd, toInternalContext(context), toInternalParams(params));
    }
}

} // namespace starforge::public_api_bridge
