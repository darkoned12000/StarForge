#include "starforge/asset.hpp"

#include "asset_bridge.hpp"

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
        mesh.material.hasBaseColorTexture = raw.material.hasBaseColorTexture;
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

} // namespace starforge
