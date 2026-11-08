#include "toon_bridge.hpp"

#include "environment/toon_renderer.hpp"

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

[[nodiscard]] environment::ToonRenderContext toInternalContext(const RawToonContext& value) noexcept
{
    environment::ToonRenderContext result;
    result.viewProj = toInternalMat4(value.viewProj);
    result.modelView = toInternalMat4(value.modelView);
    result.lightDirectionView = ::starforge::math::Vec3f{value.lightDirectionView.x, value.lightDirectionView.y,
                                                          value.lightDirectionView.z};
    result.resolution = ::starforge::math::Vec2f{value.resolution.x, value.resolution.y};
    return result;
}

[[nodiscard]] environment::ToonParams toInternalParams(const RawToonParams& value) noexcept
{
    environment::ToonParams result;
    result.baseColor = ::starforge::math::Vec4f{value.baseColor.x, value.baseColor.y, value.baseColor.z, value.baseColor.w};
    result.shadowColor = ::starforge::math::Vec4f{value.shadowColor.x, value.shadowColor.y, value.shadowColor.z,
                                                  value.shadowColor.w};
    result.bandCount = value.bandCount;
    result.rimStrength = value.rimStrength;
    result.outlineWidth = value.outlineWidth;
    return result;
}

} // namespace

struct RawToon::Impl
{
    environment::ToonRenderer renderer;
};

RawToon::~RawToon() = default;
RawToon::RawToon(RawToon&& other) noexcept : impl_(std::move(other.impl_))
{
}
RawToon& RawToon::operator=(RawToon&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<std::unique_ptr<RawToon>, std::string> RawToon::create(const CreateInfo& info)
{
    auto backend = environment::ToonRenderer::create({
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

    auto result = std::unique_ptr<RawToon>(new RawToon());
    result->impl_ = std::make_unique<Impl>(Impl{std::move(*backend)});
    return result;
}

void RawToon::render(VkCommandBuffer cmd, const RawToonContext& context, const RawToonParams& params)
{
    if (impl_)
    {
        impl_->renderer.render(cmd, toInternalContext(context), toInternalParams(params));
    }
}

} // namespace starforge::public_api_bridge
