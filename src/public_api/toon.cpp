#include "starforge/toon.hpp"

#include "renderer_impl.hpp"
#include "shader_dir.hpp"
#include "toon_bridge.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

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
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .queueFamily = RendererInternalAccess::queueFamily(renderer),
        .framesInFlight = RendererInternalAccess::framesInFlight(renderer),
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
    rawParams.midColor = toRaw(params.midColor);
    rawParams.bandCount = params.bandCount;
    rawParams.rimStrength = params.rimStrength;
    rawParams.outlineWidth = params.outlineWidth;
    rawParams.capabilities = params.capabilities;
    rawParams.emissiveColor = toRaw(params.emissiveColor);
    rawParams.fresnelColor = toRaw(params.fresnelColor);
    rawParams.materialParams = toRaw(params.materialParams);
    rawParams.highlightColor = toRaw(params.highlightColor);

    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), rawContext, rawParams);
}

struct Toon::ToonMesh::Impl
{
    std::unique_ptr<bridge::RawToonMesh> backend;
};

Toon::ToonMesh::ToonMesh(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Toon::ToonMesh::~ToonMesh() = default;
Toon::ToonMesh::ToonMesh(ToonMesh&&) noexcept = default;
Toon::ToonMesh& Toon::ToonMesh::operator=(ToonMesh&&) noexcept = default;

std::uint32_t Toon::ToonMesh::vertexCount() const noexcept
{
    return impl_ && impl_->backend ? impl_->backend->vertexCount() : 0u;
}

std::expected<Toon::ToonMesh, std::string>
Toon::createMesh(std::span<const MeshVertex> vertices) const
{
    if (!impl_ || !impl_->backend)
    {
        return std::unexpected("Toon::createMesh failed: Toon is moved-from");
    }
    if (vertices.empty())
    {
        return std::unexpected("Toon::createMesh requires at least one vertex");
    }

    std::vector<bridge::RawToonVertex> raw(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        raw[i].position[0] = vertices[i].position.x();
        raw[i].position[1] = vertices[i].position.y();
        raw[i].position[2] = vertices[i].position.z();
        raw[i].normal[0] = vertices[i].normal.x();
        raw[i].normal[1] = vertices[i].normal.y();
        raw[i].normal[2] = vertices[i].normal.z();
        raw[i].outlineNormal[0] = vertices[i].outlineNormal.x();
        raw[i].outlineNormal[1] = vertices[i].outlineNormal.y();
        raw[i].outlineNormal[2] = vertices[i].outlineNormal.z();
    }
    auto backend = impl_->backend->createMesh(raw.data(), static_cast<std::uint32_t>(raw.size()));
    if (!backend) return std::unexpected(backend.error());

    auto impl = std::make_unique<ToonMesh::Impl>();
    impl->backend = std::move(*backend);
    return ToonMesh(std::move(impl));
}

std::expected<Toon::ToonMesh, std::string>
Toon::createMeshFromAsset(const MeshData& mesh) const
{
    if (!impl_ || !impl_->backend)
        return std::unexpected("Toon::createMeshFromAsset failed: Toon is moved-from");
    if (mesh.vertices.empty()) return std::unexpected("Asset mesh has no vertices");

    // Weld by position and average normals to bake smooth outline normals.
    // Copying hard per-face normals would crack the inverted-hull outline at
    // every sharp corner (review finding).
    auto positionKey = [](const math::Vec3f& p) {
        auto key = [](float f) {
            return static_cast<std::int64_t>(std::round(f * 10000.0f));
        };
        return std::make_tuple(key(p.x()), key(p.y()), key(p.z()));
    };
    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, math::Vec3f> accumulated;
    for (const auto& v : mesh.vertices)
    {
        accumulated[positionKey(v.position)] =
            accumulated[positionKey(v.position)] + v.normal;
    }

    std::vector<bridge::RawToonTexturedVertex> raw(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const auto& v = mesh.vertices[i];
        raw[i].position[0] = v.position.x(); raw[i].position[1] = v.position.y(); raw[i].position[2] = v.position.z();
        raw[i].normal[0] = v.normal.x(); raw[i].normal[1] = v.normal.y(); raw[i].normal[2] = v.normal.z();
        const math::Vec3f smooth = math::normalize(accumulated[positionKey(v.position)]);
        raw[i].outlineNormal[0] = smooth.x();
        raw[i].outlineNormal[1] = smooth.y();
        raw[i].outlineNormal[2] = smooth.z();
        raw[i].uv[0] = v.uv.x(); raw[i].uv[1] = v.uv.y();
        for (int c = 0; c < 4; ++c) raw[i].tangent[c] = v.tangent[c];
    }

    static constexpr std::uint8_t kWhite[4] = {255, 255, 255, 255};
    const bool hasTexture = mesh.material.hasBaseColorTexture && !mesh.material.textureRGBA.empty();
    const std::uint8_t* pixels = hasTexture ? mesh.material.textureRGBA.data() : kWhite;
    const std::uint32_t width = hasTexture ? mesh.material.textureWidth : 1u;
    const std::uint32_t height = hasTexture ? mesh.material.textureHeight : 1u;

    auto backend = impl_->backend->createTexturedMesh(
        raw.data(), static_cast<std::uint32_t>(raw.size()), pixels, width, height);
    if (!backend) return std::unexpected(backend.error());
    auto impl = std::make_unique<ToonMesh::Impl>();
    impl->backend = std::move(*backend);
    return ToonMesh(std::move(impl));
}

void Toon::recordMesh(FrameContext& frameContext, const Context& context,
                      const Params& params, const ToonMesh& mesh)
{
    if (!impl_ || !impl_->backend || !mesh.impl_ || !mesh.impl_->backend) return;

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
    rawParams.midColor = toRaw(params.midColor);
    rawParams.bandCount = params.bandCount;
    rawParams.rimStrength = params.rimStrength;
    rawParams.outlineWidth = params.outlineWidth;
    rawParams.capabilities = params.capabilities;
    rawParams.emissiveColor = toRaw(params.emissiveColor);
    rawParams.fresnelColor = toRaw(params.fresnelColor);
    rawParams.materialParams = toRaw(params.materialParams);
    rawParams.highlightColor = toRaw(params.highlightColor);

    impl_->backend->renderMesh(RendererInternalAccess::commandBuffer(frameContext),
                               rawContext, rawParams, *mesh.impl_->backend);
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
