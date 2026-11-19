#pragma once

#include "starforge/math.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starforge
{

// CPU-side decoded glTF 2.0 asset. Holds positions, normals, UVs, tangents,
// triangle indices and glTF base-colour material data (including embedded
// textures). No Vulkan types are exposed; rendering is done by Toon/Mesh.
struct MeshVertexData
{
    math::Vec3f position{};
    math::Vec3f normal{};
    math::Vec2f uv{};
    math::Vec4f tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

struct MeshMaterialData
{
    math::Vec4f baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    math::Vec4f emissiveFactor{0.0f, 0.0f, 0.0f, 1.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    bool hasBaseColorTexture = false;
    bool hasNormalTexture = false;
    bool hasMetallicRoughnessTexture = false;
    bool hasOcclusionTexture = false;
    bool hasEmissiveTexture = false;
    bool doubleSided = false;
    std::string alphaMode = "OPAQUE";
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    std::vector<std::uint8_t> textureRGBA; // decoded RGBA8 pixels
    std::string name;
};

// Capability bits understood by Toon::Params::capabilities. This enum mirrors
// starforge::ToonCapability but lives here so asset.hpp stays self-contained.
enum class AssetToonCapability : std::uint32_t
{
    None = 0,
    Emissive = 1,
    Fresnel = 2,
    Faceted = 4,
    ScatteringRim = 8,
    ShadowDetail = 16,
    Textured = 32,
};

constexpr AssetToonCapability operator|(AssetToonCapability a, AssetToonCapability b) noexcept
{
    return static_cast<AssetToonCapability>(static_cast<std::uint32_t>(a) |
                                            static_cast<std::uint32_t>(b));
}
constexpr AssetToonCapability operator&(AssetToonCapability a, AssetToonCapability b) noexcept
{
    return static_cast<AssetToonCapability>(static_cast<std::uint32_t>(a) &
                                            static_cast<std::uint32_t>(b));
}

// glTF PBR material mapped onto the toon material model.
struct ToonMaterialDesc
{
    std::uint32_t capabilities = 0;
    math::Vec4f baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    math::Vec4f shadowColor{0.2f, 0.1f, 0.15f, 1.0f};
    math::Vec4f midColor{0.6f, 0.4f, 0.4f, 1.0f};
    math::Vec4f emissiveColor{0.0f, 0.0f, 0.0f, 1.0f};
    math::Vec4f fresnelColor{0.3f, 0.7f, 1.0f, 1.0f};
    float bandCount = 3.0f;
    float rimStrength = 0.6f;
    float outlineWidth = 2.0f;
    float fresnelPower = 3.0f;
    bool doubleSided = false;
    std::string name;
};

enum class MaterialVariant
{
    Default,
    Damaged,
    Shielded,
    Highlighted,
};

[[nodiscard]] ToonMaterialDesc mapAssetMaterial(const MeshMaterialData& material);
[[nodiscard]] ToonMaterialDesc applyAssetMaterialVariant(const ToonMaterialDesc& base,
                                                         MaterialVariant variant);
[[nodiscard]] std::string_view materialVariantName(MaterialVariant variant) noexcept;

struct MeshData
{
    std::vector<MeshVertexData> vertices;
    std::vector<std::uint32_t> indices; // already expanded: i-th vertex
    MeshMaterialData material;
    std::string name;
};

class Asset
{
public:
    static std::expected<Asset, std::string> loadFromMemory(std::span<const std::uint8_t> glbBytes);
    static std::expected<Asset, std::string> loadFromFile(const std::string& path);

    ~Asset();
    Asset(Asset&&) noexcept;
    Asset& operator=(Asset&&) noexcept;
    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;

    [[nodiscard]] std::span<const MeshData> meshes() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return meshes().empty(); }

private:
    struct Impl;
    explicit Asset(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
