#include "asset_bridge.hpp"

#include "asset/gltf_loader.hpp"

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

std::expected<std::unique_ptr<RawAsset>, std::string>
RawAsset::loadFromMemory(const std::uint8_t* data, std::size_t size)
{
    auto decoded = asset::loadGlb(data, size);
    if (!decoded) return std::unexpected(decoded.error());

    auto result = std::unique_ptr<RawAsset>(new RawAsset());
    auto impl = std::make_unique<Impl>();
    impl->meshes.reserve(decoded->meshes.size());
    for (const auto& mesh : decoded->meshes)
    {
        RawAssetMesh converted;
        converted.name = mesh.name;
        converted.indices = mesh.indices;
        converted.material.baseColorFactor[0] = mesh.material.baseColorFactor[0];
        converted.material.baseColorFactor[1] = mesh.material.baseColorFactor[1];
        converted.material.baseColorFactor[2] = mesh.material.baseColorFactor[2];
        converted.material.baseColorFactor[3] = mesh.material.baseColorFactor[3];
        converted.material.hasBaseColorTexture = mesh.material.hasBaseColorTexture;
        converted.material.textureWidth = mesh.material.baseColorTexture.width;
        converted.material.textureHeight = mesh.material.baseColorTexture.height;
        converted.material.textureRGBA = mesh.material.baseColorTexture.rgba;
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
        impl->meshes.push_back(std::move(converted));
    }
    result->impl_ = std::move(impl);
    return result;
}

std::span<const RawAssetMesh> RawAsset::meshes() const noexcept
{
    if (!impl_) return {};
    return impl_->meshes;
}

} // namespace starforge::public_api_bridge
