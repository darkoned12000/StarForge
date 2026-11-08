#include "starforge/toon.hpp"

#include "renderer_impl.hpp"
#include "shader_dir.hpp"
#include "toon_bridge.hpp"

#include <utility>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

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

[[nodiscard]] bridge::RawToon::CreateInfo makeBackendInfo(Renderer& renderer, const std::string& shaderDirectory)
{
    return bridge::RawToon::CreateInfo{
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
    return public_api_detail::resolveShaderDirectory(requested, "toon.vert.spv");
}

} // namespace

struct Toon::Impl
{
    std::unique_ptr<bridge::RawToon> backend;
    std::string shaderDirectory;
};

Toon::Toon(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

Toon::~Toon() = default;

Toon::Toon(Toon&& other) noexcept : impl_(std::move(other.impl_))
{
}

Toon& Toon::operator=(Toon&& other) noexcept
{
    impl_ = std::move(other.impl_);
    return *this;
}

std::expected<Toon, std::string> Toon::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Toon::create failed: Renderer is invalid or moved-from");
    }

    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawToon::create(makeBackendInfo(renderer, shaderDirectory));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }

    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    return Toon(std::move(impl));
}

void Toon::record(FrameContext& frameContext, const Context& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return;
    }

    bridge::RawToonContext rawContext;
    rawContext.viewProj = toRaw(context.viewProj);
    rawContext.modelView = toRaw(context.modelView);
    rawContext.lightDirectionView = toRaw(context.lightDirectionView);

    const Size2D extent = frameContext.extent();
    const math::Vec2f resolution = context.resolution.x() <= 0.0f || context.resolution.y() <= 0.0f
                                       ? math::Vec2f{static_cast<float>(extent.width),
                                                     static_cast<float>(extent.height)}
                                       : context.resolution;
    rawContext.resolution = toRaw(resolution);

    bridge::RawToonParams rawParams;
    rawParams.baseColor = toRaw(params.baseColor);
    rawParams.shadowColor = toRaw(params.shadowColor);
    rawParams.bandCount = params.bandCount;
    rawParams.rimStrength = params.rimStrength;
    rawParams.outlineWidth = params.outlineWidth;

    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), rawContext, rawParams);
}

std::expected<Renderer::FrameResult, std::string> Toon::render(Renderer& renderer, const Context& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return std::unexpected("Toon is moved-from");
    }

    return renderer.renderFrame(
        Renderer::ClearColor{}, [this, &context, &params](FrameContext& frameContext) {
            record(frameContext, context, params);
        });
}

std::expected<void, std::string> Toon::recreate(Renderer& renderer)
{
    if (!impl_)
    {
        return std::unexpected("Toon is moved-from");
    }
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Toon::recreate failed: Renderer is invalid or moved-from");
    }

    renderer.waitIdle();
    auto backend = bridge::RawToon::create(makeBackendInfo(renderer, impl_->shaderDirectory));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }
    impl_->backend = std::move(*backend);
    return {};
}

} // namespace starforge
