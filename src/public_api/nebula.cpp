#include "starforge/nebula.hpp"

#include "renderer_impl.hpp"
#include "shader_dir.hpp"
#include "nebula_bridge.hpp"

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

[[nodiscard]] bridge::RawNebula::CreateInfo makeBackendInfo(Renderer& renderer, const std::string& shaderDirectory)
{
    return bridge::RawNebula::CreateInfo{
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
    return public_api_detail::resolveShaderDirectory(requested, "nebula.vert.spv");
}

} // namespace

struct Nebula::Impl
{
    std::unique_ptr<bridge::RawNebula> backend;
    std::string shaderDirectory;
};

Nebula::Nebula(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Nebula::~Nebula() = default;
Nebula::Nebula(Nebula&& other) noexcept : impl_(std::move(other.impl_)) {}
Nebula& Nebula::operator=(Nebula&& other) noexcept
{
    impl_ = std::move(other.impl_);
    return *this;
}

std::expected<Nebula, std::string> Nebula::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Nebula::create failed: Renderer is invalid or moved-from");
    }
    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawNebula::create(makeBackendInfo(renderer, shaderDirectory));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    return Nebula(std::move(impl));
}

void Nebula::record(FrameContext& frameContext, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return;
    }
    bridge::RawNebulaContext rawContext;
    rawContext.viewProj = toRaw(context.viewProj);
    rawContext.invViewProj = toRaw(context.invViewProj);
    rawContext.cameraPosition = toRaw(context.cameraPosition);
    const Size2D extent = frameContext.extent();
    const math::Vec2f resolution = (context.resolution.x() <= 0.0f || context.resolution.y() <= 0.0f)
                                       ? math::Vec2f{static_cast<float>(extent.width), static_cast<float>(extent.height)}
                                       : context.resolution;
    rawContext.resolution = toRaw(resolution);
    rawContext.time = context.time;

    bridge::RawNebulaParams rawParams;
    rawParams.baseColor = toRaw(params.baseColor);
    rawParams.density = params.density;
    rawParams.absorption = params.absorption;
    rawParams.scattering = params.scattering;
    rawParams.time = params.time;
    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), rawContext, rawParams);
}

std::expected<Renderer::FrameResult, std::string>
Nebula::render(Renderer& renderer, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return std::unexpected("Nebula is moved-from");
    }
    return renderer.renderFrame({}, [this, &context, &params](FrameContext& frameContext) {
        record(frameContext, context, params);
    });
}

std::expected<void, std::string> Nebula::recreate(Renderer& renderer)
{
    if (!impl_)
    {
        return std::unexpected("Nebula is moved-from");
    }
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Nebula::recreate failed: Renderer is invalid or moved-from");
    }
    renderer.waitIdle();
    auto backend = bridge::RawNebula::create(makeBackendInfo(renderer, impl_->shaderDirectory));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }
    impl_->backend = std::move(*backend);
    return {};
}

} // namespace starforge
