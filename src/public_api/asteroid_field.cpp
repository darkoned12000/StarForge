#include "starforge/asteroid_field.hpp"

#include "asteroid_field_bridge.hpp"
#include "renderer_impl.hpp"
#include "shader_dir.hpp"

#include <string>
#include <utility>
#include <vector>

namespace starforge
{
namespace
{
namespace bridge = public_api_bridge;

[[nodiscard]] bridge::RawMat4f toRaw(const math::Mat4f& value) noexcept
{
    bridge::RawMat4f result;
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            result.columns[c][r] = value.columns[c][r];
    return result;
}

[[nodiscard]] std::string resolveShaderDirectory(const std::string& requested)
{
    return public_api_detail::resolveShaderDirectory(requested, "rock_field.vert.spv");
}

[[nodiscard]] bridge::RawAsteroidField::CreateInfo makeBackendInfo(Renderer& renderer,
                                                                   const std::string& shaderDirectory,
                                                                   const AsteroidField::CreateInfo& info)
{
    return bridge::RawAsteroidField::CreateInfo{
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .colorFormat = RendererInternalAccess::colorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = shaderDirectory,
        .variantCount = info.variantCount,
        .capacity = info.capacity,
    };
}
} // namespace

struct AsteroidField::Impl
{
    std::unique_ptr<bridge::RawAsteroidField> backend;
    std::string shaderDirectory;
    CreateInfo createInfo;
};

AsteroidField::AsteroidField(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
AsteroidField::~AsteroidField() = default;
AsteroidField::AsteroidField(AsteroidField&&) noexcept = default;
AsteroidField& AsteroidField::operator=(AsteroidField&&) noexcept = default;

std::expected<AsteroidField, std::string> AsteroidField::create(Renderer& renderer,
                                                                const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
        return std::unexpected("AsteroidField::create failed: Renderer is invalid or moved-from");
    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawAsteroidField::create(makeBackendInfo(renderer, shaderDirectory, info));
    if (!backend) return std::unexpected(backend.error());
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    impl->createInfo = info;
    return AsteroidField(std::move(impl));
}

void AsteroidField::record(FrameContext& frameContext, const RenderContext& context,
                           const Instance* instances, std::uint32_t count)
{
    if (!impl_ || !impl_->backend) return;

    bridge::RawRockRenderContext raw;
    raw.viewProj = toRaw(context.viewProj);
    raw.projectionScale = bridge::RawVec2f{context.projectionScale.x(), context.projectionScale.y()};
    const Size2D extent = frameContext.extent();
    const math::Vec2f resolution = (context.resolution.x() <= 0.0f || context.resolution.y() <= 0.0f)
                                       ? math::Vec2f{static_cast<float>(extent.width),
                                                     static_cast<float>(extent.height)}
                                       : context.resolution;
    raw.resolution = bridge::RawVec2f{resolution.x(), resolution.y()};
    raw.sunDirection = bridge::RawVec3f{context.sunDirection.x(), context.sunDirection.y(),
                                        context.sunDirection.z()};
    raw.cameraPosition = bridge::RawVec3f{context.cameraPosition.x(), context.cameraPosition.y(),
                                          context.cameraPosition.z()};
    raw.lodDistance = context.lodDistance;
    raw.outlineWidthPixels = context.outlineWidthPixels;
    raw.outline = context.outline;

    // Convert public instances to the plain-data bridge layout.
    std::vector<bridge::RawRockInstance> rawInstances;
    if (instances && count > 0)
    {
        rawInstances.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto& instance = instances[i];
            bridge::RawRockInstance rawInstance;
            rawInstance.position[0] = instance.position.x();
            rawInstance.position[1] = instance.position.y();
            rawInstance.position[2] = instance.position.z();
            rawInstance.scale = instance.scale;
            rawInstance.rotation[0] = instance.rotation.x();
            rawInstance.rotation[1] = instance.rotation.y();
            rawInstance.rotation[2] = instance.rotation.z();
            rawInstance.rotation[3] = instance.rotation.w();
            rawInstance.tint[0] = instance.tint.x();
            rawInstance.tint[1] = instance.tint.y();
            rawInstance.tint[2] = instance.tint.z();
            rawInstance.tint[3] = instance.tint.w();
            rawInstance.seed = instance.seed;
            rawInstances.push_back(rawInstance);
        }
    }
    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), raw,
                           rawInstances.empty() ? nullptr : rawInstances.data(), count);
}

std::expected<void, std::string> AsteroidField::recreate(Renderer& renderer)
{
    if (!impl_ || !impl_->backend) return std::unexpected("AsteroidField is moved-from");
    renderer.waitIdle();
    return impl_->backend->recreate(makeBackendInfo(renderer, impl_->shaderDirectory, impl_->createInfo));
}

} // namespace starforge
