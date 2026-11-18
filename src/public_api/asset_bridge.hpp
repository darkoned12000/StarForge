#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Internal bridge: plain PODs only, so no TU sees both public and internal math.
namespace starforge::public_api_bridge
{

struct RawAssetVertex
{
    float position[3] = {};
    float normal[3] = {};
    float uv[2] = {};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
};

struct RawAssetMaterial
{
    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool hasBaseColorTexture = false;
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    std::vector<std::uint8_t> textureRGBA;
    std::string name;
};

struct RawAssetMesh
{
    std::vector<RawAssetVertex> vertices;
    std::vector<std::uint32_t> indices;
    RawAssetMaterial material;
    std::string name;
};

class RawAsset
{
public:
    static std::expected<std::unique_ptr<RawAsset>, std::string> loadFromMemory(const std::uint8_t* data, std::size_t size);
    ~RawAsset();
    RawAsset(RawAsset&&) noexcept;
    RawAsset& operator=(RawAsset&&) noexcept;
    RawAsset(const RawAsset&) = delete;
    RawAsset& operator=(const RawAsset&) = delete;

    [[nodiscard]] std::span<const RawAssetMesh> meshes() const noexcept;

private:
    RawAsset() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
