#include "starforge/asset.hpp"

#include "asset_bridge.hpp"
#include "asset/file_watcher.hpp"
#include "asset/material_mapping.hpp"
#include "asset/mesh_optimizer.hpp"
#include "asset/texture_streamer.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace starforge
{

namespace
{
namespace bridge = public_api_bridge;
} // namespace

struct Asset::Impl
{
    std::vector<MeshData> meshes;
};

struct StreamingTexture::Impl
{
    asset::StreamingTexture texture;
    explicit Impl(asset::StreamingTexture t) : texture(std::move(t)) {}
};

struct AssetWatcher::Impl
{
    asset::FileWatcher watcher;
};

Asset::Asset(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Asset::~Asset() = default;
Asset::Asset(Asset&&) noexcept = default;
Asset& Asset::operator=(Asset&&) noexcept = default;

std::expected<Asset, std::string> Asset::loadFromMemory(std::span<const std::uint8_t> glbBytes)
{
    auto decoded = bridge::RawAsset::loadFromMemory(glbBytes.data(), glbBytes.size());
    if (!decoded) return std::unexpected(decoded.error());

    auto impl = std::make_unique<Impl>();
    for (const auto& raw : (*decoded)->meshes())
    {
        MeshData mesh;
        mesh.name = raw.name;
        mesh.indices = raw.indices;
        mesh.material.baseColorFactor = math::Vec4f{
            raw.material.baseColorFactor[0], raw.material.baseColorFactor[1],
            raw.material.baseColorFactor[2], raw.material.baseColorFactor[3]};
        mesh.material.emissiveFactor = math::Vec4f{
            raw.material.emissiveFactor[0], raw.material.emissiveFactor[1],
            raw.material.emissiveFactor[2], raw.material.emissiveFactor[3]};
        mesh.material.metallicFactor = raw.material.metallicFactor;
        mesh.material.roughnessFactor = raw.material.roughnessFactor;
        mesh.material.hasBaseColorTexture = raw.material.hasBaseColorTexture;
        mesh.material.hasNormalTexture = raw.material.hasNormalTexture;
        mesh.material.hasMetallicRoughnessTexture = raw.material.hasMetallicRoughnessTexture;
        mesh.material.hasOcclusionTexture = raw.material.hasOcclusionTexture;
        mesh.material.hasEmissiveTexture = raw.material.hasEmissiveTexture;
        mesh.material.doubleSided = raw.material.doubleSided;
        mesh.material.alphaMode = raw.material.alphaMode;
        mesh.material.textureWidth = raw.material.textureWidth;
        mesh.material.textureHeight = raw.material.textureHeight;
        mesh.material.textureRGBA = raw.material.textureRGBA;
        mesh.material.name = raw.material.name;
        mesh.vertices.reserve(raw.vertices.size());
        for (const auto& vertex : raw.vertices)
        {
            mesh.vertices.push_back(MeshVertexData{
                math::Vec3f{vertex.position[0], vertex.position[1], vertex.position[2]},
                math::Vec3f{vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                math::Vec2f{vertex.uv[0], vertex.uv[1]},
                math::Vec4f{vertex.tangent[0], vertex.tangent[1], vertex.tangent[2], vertex.tangent[3]}});
        }
        impl->meshes.push_back(std::move(mesh));
    }
    return Asset(std::move(impl));
}

std::expected<Asset, std::string> Asset::loadFromFile(const std::string& path)
{
    // The bridge resolves external .gltf buffer/image URIs relative to path;
    // loadFromMemory remains the GLB-only entry point.
    auto decoded = bridge::RawAsset::loadFromFile(path);
    if (!decoded) return std::unexpected(decoded.error());

    auto impl = std::make_unique<Impl>();
    for (const auto& raw : (*decoded)->meshes())
    {
        MeshData mesh;
        mesh.name = raw.name;
        mesh.indices = raw.indices;
        mesh.material.baseColorFactor = math::Vec4f{
            raw.material.baseColorFactor[0], raw.material.baseColorFactor[1],
            raw.material.baseColorFactor[2], raw.material.baseColorFactor[3]};
        mesh.material.emissiveFactor = math::Vec4f{
            raw.material.emissiveFactor[0], raw.material.emissiveFactor[1],
            raw.material.emissiveFactor[2], raw.material.emissiveFactor[3]};
        mesh.material.metallicFactor = raw.material.metallicFactor;
        mesh.material.roughnessFactor = raw.material.roughnessFactor;
        mesh.material.hasBaseColorTexture = raw.material.hasBaseColorTexture;
        mesh.material.hasNormalTexture = raw.material.hasNormalTexture;
        mesh.material.hasMetallicRoughnessTexture = raw.material.hasMetallicRoughnessTexture;
        mesh.material.hasOcclusionTexture = raw.material.hasOcclusionTexture;
        mesh.material.hasEmissiveTexture = raw.material.hasEmissiveTexture;
        mesh.material.doubleSided = raw.material.doubleSided;
        mesh.material.alphaMode = raw.material.alphaMode;
        mesh.material.textureWidth = raw.material.textureWidth;
        mesh.material.textureHeight = raw.material.textureHeight;
        mesh.material.textureRGBA = raw.material.textureRGBA;
        mesh.material.name = raw.material.name;
        mesh.vertices.reserve(raw.vertices.size());
        for (const auto& vertex : raw.vertices)
            mesh.vertices.push_back(MeshVertexData{
                math::Vec3f{vertex.position[0], vertex.position[1], vertex.position[2]},
                math::Vec3f{vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                math::Vec2f{vertex.uv[0], vertex.uv[1]},
                math::Vec4f{vertex.tangent[0], vertex.tangent[1], vertex.tangent[2], vertex.tangent[3]}});
        impl->meshes.push_back(std::move(mesh));
    }
    return Asset(std::move(impl));
}

std::span<const MeshData> Asset::meshes() const noexcept
{
    if (!impl_) return {};
    return impl_->meshes;
}

std::expected<StreamingTexture, std::string>
StreamingTexture::fromRGBA(std::uint32_t width, std::uint32_t height,
                           std::span<const std::uint8_t> rgba, std::size_t budgetBytes)
{
    if (width == 0 || height == 0 || rgba.size() < static_cast<std::size_t>(width) * height * 4)
        return std::unexpected("Invalid RGBA texture dimensions or pixel count");
    asset::TextureData base;
    base.width = width;
    base.height = height;
    base.rgba.assign(rgba.begin(), rgba.end());
    return StreamingTexture(std::make_unique<Impl>(
        asset::StreamingTexture(std::move(base), budgetBytes)));
}

StreamingTexture::StreamingTexture() noexcept
    : impl_(std::make_unique<Impl>(asset::StreamingTexture())) {}

StreamingTexture::StreamingTexture(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
StreamingTexture::~StreamingTexture() = default;
StreamingTexture::StreamingTexture(StreamingTexture&&) noexcept = default;
StreamingTexture& StreamingTexture::operator=(StreamingTexture&&) noexcept = default;

std::size_t StreamingTexture::mipCount() const noexcept { return impl_ ? impl_->texture.mipCount() : 0; }
std::uint32_t StreamingTexture::width(std::size_t level) const noexcept
{
    return impl_ ? impl_->texture.chain().level(level).width : 0;
}
std::uint32_t StreamingTexture::height(std::size_t level) const noexcept
{
    return impl_ ? impl_->texture.chain().level(level).height : 0;
}
std::span<const std::uint8_t> StreamingTexture::data(std::size_t level) const noexcept
{
    const auto* pixels = impl_ ? impl_->texture.data(level) : nullptr;
    return pixels ? std::span<const std::uint8_t>(pixels->rgba) : std::span<const std::uint8_t>{};
}
bool StreamingTexture::request(std::size_t level) noexcept
{
    return impl_ && impl_->texture.request(level) != nullptr;
}
bool StreamingTexture::resident(std::size_t level) const noexcept
{
    return impl_ && impl_->texture.resident(level);
}
std::size_t StreamingTexture::selectMip(std::uint32_t screenPixels) const noexcept
{
    return impl_ ? impl_->texture.chain().selectLevel(screenPixels) : 0;
}
std::size_t StreamingTexture::budget() const noexcept { return impl_ ? impl_->texture.budget() : 0; }
std::size_t StreamingTexture::residentBytes() const noexcept
{
    return impl_ ? impl_->texture.residentBytes() : 0;
}
void StreamingTexture::setBudget(std::size_t bytes) noexcept
{
    if (impl_) impl_->texture.setBudget(bytes);
}

namespace
{
asset::MaterialData toInternal(const MeshMaterialData& material)
{
    asset::MaterialData out;
    for (std::size_t i = 0; i < 4; ++i)
    {
        out.baseColorFactor[i] = material.baseColorFactor[i];
        out.emissiveFactor[i] = material.emissiveFactor[i];
    }
    out.metallicFactor = material.metallicFactor;
    out.roughnessFactor = material.roughnessFactor;
    out.hasBaseColorTexture = material.hasBaseColorTexture;
    out.hasNormalTexture = material.hasNormalTexture;
    out.hasMetallicRoughnessTexture = material.hasMetallicRoughnessTexture;
    out.hasOcclusionTexture = material.hasOcclusionTexture;
    out.hasEmissiveTexture = material.hasEmissiveTexture;
    out.doubleSided = material.doubleSided;
    out.alphaMode = material.alphaMode;
    out.name = material.name;
    return out;
}

asset::MeshData toInternalMesh(const MeshData& mesh)
{
    asset::MeshData out;
    out.name = mesh.name;
    out.indices = mesh.indices;
    out.material = toInternal(mesh.material);
    out.material.baseColorTexture.width = mesh.material.textureWidth;
    out.material.baseColorTexture.height = mesh.material.textureHeight;
    out.material.baseColorTexture.rgba = mesh.material.textureRGBA;
    out.vertices.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices)
    {
        asset::MeshVertexData v;
        v.position[0] = vertex.position.x(); v.position[1] = vertex.position.y(); v.position[2] = vertex.position.z();
        v.normal[0] = vertex.normal.x(); v.normal[1] = vertex.normal.y(); v.normal[2] = vertex.normal.z();
        v.uv[0] = vertex.uv.x(); v.uv[1] = vertex.uv.y();
        for (int i = 0; i < 4; ++i) v.tangent[i] = vertex.tangent[i];
        out.vertices.push_back(v);
    }
    return out;
}

ToonMaterialDesc toPublic(const asset::ToonMaterialDesc& desc)
{
    ToonMaterialDesc out;
    out.capabilities = desc.capabilities;
    out.baseColor = math::Vec4f{desc.baseColor[0], desc.baseColor[1], desc.baseColor[2], desc.baseColor[3]};
    out.shadowColor = math::Vec4f{desc.shadowColor[0], desc.shadowColor[1], desc.shadowColor[2], desc.shadowColor[3]};
    out.midColor = math::Vec4f{desc.midColor[0], desc.midColor[1], desc.midColor[2], desc.midColor[3]};
    out.emissiveColor = math::Vec4f{desc.emissiveColor[0], desc.emissiveColor[1], desc.emissiveColor[2], desc.emissiveColor[3]};
    out.fresnelColor = math::Vec4f{desc.fresnelColor[0], desc.fresnelColor[1], desc.fresnelColor[2], desc.fresnelColor[3]};
    out.bandCount = desc.bandCount;
    out.rimStrength = desc.rimStrength;
    out.outlineWidth = desc.outlineWidth;
    out.fresnelPower = desc.fresnelPower;
    out.doubleSided = desc.doubleSided;
    out.name = desc.name;
    return out;
}
} // namespace

namespace
{
MeshData toPublicMesh(const asset::MeshData& source)
{
    MeshData mesh;
    mesh.name = source.name;
    mesh.indices = source.indices;
    mesh.material.baseColorFactor = math::Vec4f{
        source.material.baseColorFactor[0], source.material.baseColorFactor[1],
        source.material.baseColorFactor[2], source.material.baseColorFactor[3]};
    mesh.material.emissiveFactor = math::Vec4f{
        source.material.emissiveFactor[0], source.material.emissiveFactor[1],
        source.material.emissiveFactor[2], source.material.emissiveFactor[3]};
    mesh.material.metallicFactor = source.material.metallicFactor;
    mesh.material.roughnessFactor = source.material.roughnessFactor;
    mesh.material.hasBaseColorTexture = source.material.hasBaseColorTexture;
    mesh.material.hasNormalTexture = source.material.hasNormalTexture;
    mesh.material.hasMetallicRoughnessTexture = source.material.hasMetallicRoughnessTexture;
    mesh.material.hasOcclusionTexture = source.material.hasOcclusionTexture;
    mesh.material.hasEmissiveTexture = source.material.hasEmissiveTexture;
    mesh.material.doubleSided = source.material.doubleSided;
    mesh.material.alphaMode = source.material.alphaMode;
    mesh.material.textureWidth = source.material.baseColorTexture.width;
    mesh.material.textureHeight = source.material.baseColorTexture.height;
    mesh.material.textureRGBA = source.material.baseColorTexture.rgba;
    mesh.material.name = source.material.name;
    mesh.vertices.reserve(source.vertices.size());
    for (const auto& vertex : source.vertices)
        mesh.vertices.push_back(MeshVertexData{
            math::Vec3f{vertex.position[0], vertex.position[1], vertex.position[2]},
            math::Vec3f{vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            math::Vec2f{vertex.uv[0], vertex.uv[1]},
            math::Vec4f{vertex.tangent[0], vertex.tangent[1], vertex.tangent[2], vertex.tangent[3]}});
    return mesh;
}
} // namespace

std::expected<std::vector<MeshData>, std::string>
generateAssetLods(const MeshData& base, std::span<const float> ratios)
{
    // Convert to the internal layout (expanded flat vertices), build LODs,
    // then convert back so callers only see public math types.
    asset::MeshData internal = toInternalMesh(base);
    std::vector<float> r(ratios.begin(), ratios.end());
    auto levels = asset::generateLodLevels(internal, r);
    if (!levels) return std::unexpected(levels.error());
    std::vector<MeshData> result;
    result.reserve(levels->size());
    for (const auto& level : *levels) result.push_back(toPublicMesh(level));
    return result;
}

AssetLodSelector::AssetLodSelector(std::vector<float> maxDistances) noexcept
    : distances_(std::move(maxDistances))
{
}

std::size_t AssetLodSelector::update(float distance, float hysteresis) noexcept
{
    if (distances_.empty()) return 0;
    if (current_ >= distances_.size()) current_ = distances_.size() - 1;
    if (distance > distances_[current_] + hysteresis && current_ + 1 < distances_.size())
        ++current_;
    else if (current_ > 0 && distance + hysteresis < distances_[current_ - 1])
        --current_;
    return current_;
}

AssetWatcher::AssetWatcher() : impl_(std::make_unique<Impl>()) {}
AssetWatcher::~AssetWatcher() = default;
AssetWatcher::AssetWatcher(AssetWatcher&&) noexcept = default;
AssetWatcher& AssetWatcher::operator=(AssetWatcher&&) noexcept = default;

bool AssetWatcher::watch(const std::string& path) { return impl_->watcher.watch(path); }

std::vector<std::string> AssetWatcher::poll()
{
    std::vector<std::string> changed;
    for (const auto& event : impl_->watcher.poll())
        if (event.change == asset::FileChange::Modified) changed.push_back(event.path);
    return changed;
}

ToonMaterialDesc mapAssetMaterial(const MeshMaterialData& material)
{
    return toPublic(asset::mapMaterial(toInternal(material)));
}

ToonMaterialDesc applyAssetMaterialVariant(const ToonMaterialDesc& base, MaterialVariant variant)
{
    ToonMaterialDesc result = base;
    asset::ToonMaterialDesc internal;
    internal.capabilities = base.capabilities;
    for (std::size_t i = 0; i < 4; ++i)
    {
        internal.baseColor[i] = base.baseColor[i];
        internal.emissiveColor[i] = base.emissiveColor[i];
    }
    internal.name = base.name;
    internal = asset::applyMaterialVariant(internal, static_cast<asset::MaterialVariant>(variant));
    const auto mapped = toPublic(internal);
    result.capabilities = mapped.capabilities;
    result.baseColor = mapped.baseColor;
    result.shadowColor = mapped.shadowColor;
    result.midColor = mapped.midColor;
    result.emissiveColor = mapped.emissiveColor;
    result.fresnelColor = mapped.fresnelColor;
    result.bandCount = mapped.bandCount;
    result.rimStrength = mapped.rimStrength;
    result.outlineWidth = mapped.outlineWidth;
    result.fresnelPower = mapped.fresnelPower;
    result.name = mapped.name;
    return result;
}

std::string_view materialVariantName(MaterialVariant variant) noexcept
{
    return asset::materialVariantName(static_cast<asset::MaterialVariant>(variant));
}

} // namespace starforge
