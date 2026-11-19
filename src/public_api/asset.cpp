#include "starforge/asset.hpp"

#include "asset_bridge.hpp"
#include "asset/material_mapping.hpp"

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
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::unexpected("Cannot open glTF file: " + path);
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return loadFromMemory(bytes);
}

std::span<const MeshData> Asset::meshes() const noexcept
{
    if (!impl_) return {};
    return impl_->meshes;
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
    result.emissiveColor = mapped.emissiveColor;
    result.fresnelColor = mapped.fresnelColor;
    result.fresnelPower = mapped.fresnelPower;
    result.rimStrength = mapped.rimStrength;
    result.name = mapped.name;
    return result;
}

std::string_view materialVariantName(MaterialVariant variant) noexcept
{
    return asset::materialVariantName(static_cast<asset::MaterialVariant>(variant));
}

} // namespace starforge
