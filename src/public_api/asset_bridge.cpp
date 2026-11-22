#include "asset_bridge.hpp"

#include "asset/gltf_loader.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

namespace starforge::public_api_bridge
{

struct RawAsset::Impl
{
    std::vector<RawAssetMesh> meshes;
};

RawAsset::~RawAsset() = default;
RawAsset::RawAsset(RawAsset&&) noexcept = default;
RawAsset& RawAsset::operator=(RawAsset&&) noexcept = default;

namespace
{
RawAssetMesh convertMesh(const asset::MeshData& mesh)
{
    RawAssetMesh converted;
    converted.name = mesh.name;
    converted.indices = mesh.indices;
    converted.material.baseColorFactor[0] = mesh.material.baseColorFactor[0];
    converted.material.baseColorFactor[1] = mesh.material.baseColorFactor[1];
    converted.material.baseColorFactor[2] = mesh.material.baseColorFactor[2];
    converted.material.baseColorFactor[3] = mesh.material.baseColorFactor[3];
    for (int i = 0; i < 4; ++i)
        converted.material.emissiveFactor[i] = mesh.material.emissiveFactor[i];
    converted.material.metallicFactor = mesh.material.metallicFactor;
    converted.material.roughnessFactor = mesh.material.roughnessFactor;
    converted.material.hasBaseColorTexture = mesh.material.hasBaseColorTexture;
    converted.material.hasNormalTexture = mesh.material.hasNormalTexture;
    converted.material.hasMetallicRoughnessTexture = mesh.material.hasMetallicRoughnessTexture;
    converted.material.hasOcclusionTexture = mesh.material.hasOcclusionTexture;
    converted.material.hasEmissiveTexture = mesh.material.hasEmissiveTexture;
    converted.material.doubleSided = mesh.material.doubleSided;
    converted.material.alphaMode = mesh.material.alphaMode;
    converted.material.textureWidth = mesh.material.baseColorTexture.width;
    converted.material.textureHeight = mesh.material.baseColorTexture.height;
    converted.material.textureRGBA = mesh.material.baseColorTexture.rgba;
    converted.material.emissiveTextureWidth = mesh.material.emissiveTexture.width;
    converted.material.emissiveTextureHeight = mesh.material.emissiveTexture.height;
    converted.material.emissiveTextureRGBA = mesh.material.emissiveTexture.rgba;
    converted.material.name = mesh.material.name;
    converted.vertices.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices)
    {
        RawAssetVertex v;
        for (int i = 0; i < 3; ++i)
        {
            v.position[i] = vertex.position[i];
            v.normal[i] = vertex.normal[i];
        }
        for (int i = 0; i < 2; ++i) v.uv[i] = vertex.uv[i];
        for (int i = 0; i < 4; ++i) v.tangent[i] = vertex.tangent[i];
        converted.vertices.push_back(v);
    }
    return converted;
}

} // namespace

std::expected<std::unique_ptr<RawAsset>, std::string>
RawAsset::fromMeshes(std::vector<RawAssetMesh> meshes)
{
    auto result = std::unique_ptr<RawAsset>(new RawAsset());
    auto impl = std::make_unique<Impl>();
    impl->meshes = std::move(meshes);
    result->impl_ = std::move(impl);
    return result;
}

std::expected<std::unique_ptr<RawAsset>, std::string>
RawAsset::loadFromMemory(const std::uint8_t* data, std::size_t size)
{
    auto decoded = asset::loadGlb(data, size);
    if (!decoded) return std::unexpected(decoded.error());
    std::vector<RawAssetMesh> meshes;
    meshes.reserve(decoded->meshes.size());
    for (const auto& mesh : decoded->meshes)
        meshes.push_back(convertMesh(mesh));
    return fromMeshes(std::move(meshes));
}

std::expected<std::unique_ptr<RawAsset>, std::string>
RawAsset::loadFromFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::unexpected("Cannot open glTF file: " + path);
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    const auto baseDir = std::filesystem::path(path).parent_path().generic_string();
    auto decoded = asset::loadGltf(bytes.data(), bytes.size(), baseDir);
    if (!decoded) return std::unexpected(decoded.error());
    std::vector<RawAssetMesh> meshes;
    meshes.reserve(decoded->meshes.size());
    for (const auto& mesh : decoded->meshes)
        meshes.push_back(convertMesh(mesh));
    return fromMeshes(std::move(meshes));
}

std::span<const RawAssetMesh> RawAsset::meshes() const noexcept
{
    if (!impl_) return {};
    return impl_->meshes;
}

} // namespace starforge::public_api_bridge
