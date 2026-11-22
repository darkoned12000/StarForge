#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::asset
{

struct MeshVertexData
{
    float position[3] = {};
    float normal[3] = {};
    float uv[2] = {};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
};

struct TextureData
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

struct MaterialData
{
    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float emissiveFactor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    bool hasBaseColorTexture = false;
    bool hasNormalTexture = false;
    bool hasMetallicRoughnessTexture = false;
    bool hasOcclusionTexture = false;
    bool hasEmissiveTexture = false;
    bool doubleSided = false;
    std::string alphaMode = "OPAQUE";
    TextureData baseColorTexture;
    TextureData emissiveTexture;
    std::string name;
};

// Decoded, indexed glTF mesh with surface attributes and material data.
// Fully CPU-side; no Vulkan or engine math types.
struct MeshData
{
    std::vector<MeshVertexData> vertices;
    std::vector<std::uint32_t> indices;
    MaterialData material;
    std::string name;
};

struct GltfAsset
{
    std::vector<MeshData> meshes;
};

// Parses a binary glTF (GLB) byte buffer. Embedded textures are decoded to RGBA.
[[nodiscard]] std::expected<GltfAsset, std::string> loadGlb(const std::uint8_t* data, std::size_t size);

// Parses either GLB or ASCII .gltf. External buffer/image URIs are resolved
// relative to `baseDir`; data: URIs are decoded in memory.
[[nodiscard]] std::expected<GltfAsset, std::string>
loadGltf(const std::uint8_t* data, std::size_t size, const std::string& baseDir);

} // namespace starforge::asset
