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
    std::uint32_t emissiveTextureWidth = 0;
    std::uint32_t emissiveTextureHeight = 0;
    std::vector<std::uint8_t> emissiveTextureRGBA; // decoded RGBA8 pixels
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
    math::Vec4f shadowColor{0.10f, 0.14f, 0.24f, 1.0f};
    math::Vec4f midColor{0.6f, 0.4f, 0.4f, 1.0f};
    math::Vec4f emissiveColor{0.0f, 0.0f, 0.0f, 1.0f};
    math::Vec4f fresnelColor{0.3f, 0.7f, 1.0f, 1.0f};
    float bandCount = 3.0f;
    float rimStrength = 0.6f;
    float outlineWidth = 1.5f;
    float fresnelPower = 3.0f;
    bool doubleSided = false;
    std::string name;
};

struct MeshData
{
    std::vector<MeshVertexData> vertices;
    std::vector<std::uint32_t> indices; // already expanded: i-th vertex
    MeshMaterialData material;
    std::string name;
};

enum class MaterialVariant
{
    Default,
    Damaged,
    Shielded,
    Highlighted,
};

// M17.5: budgeted mip residency for streamed textures. Pixel data stays on
// the CPU; callers upload the currently resident mip through Toon.
class StreamingTexture
{
public:
    StreamingTexture() noexcept;

    static std::expected<StreamingTexture, std::string>
    fromRGBA(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba,
             std::size_t budgetBytes = 4u * 1024u * 1024u);

    ~StreamingTexture();
    StreamingTexture(StreamingTexture&&) noexcept;
    StreamingTexture& operator=(StreamingTexture&&) noexcept;
    StreamingTexture(const StreamingTexture&) = delete;
    StreamingTexture& operator=(const StreamingTexture&) = delete;

    [[nodiscard]] std::size_t mipCount() const noexcept;
    [[nodiscard]] std::uint32_t width(std::size_t level) const noexcept;
    [[nodiscard]] std::uint32_t height(std::size_t level) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> data(std::size_t level) const noexcept;

    [[nodiscard]] bool request(std::size_t level) noexcept;
    [[nodiscard]] bool resident(std::size_t level) const noexcept;
    [[nodiscard]] std::size_t selectMip(std::uint32_t screenPixels) const noexcept;
    [[nodiscard]] std::size_t budget() const noexcept;
    [[nodiscard]] std::size_t residentBytes() const noexcept;
    void setBudget(std::size_t bytes) noexcept;

private:
    struct Impl;
    explicit StreamingTexture(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

// M17.4: generates simplified LOD levels (index 0 = the optimised base).
[[nodiscard]] std::expected<std::vector<MeshData>, std::string>
generateAssetLods(const MeshData& base, std::span<const float> ratios);

// M17.4: distance-based LOD selection with hysteresis, over an ordered list
// of level distances (finest first).
class AssetLodSelector
{
public:
    explicit AssetLodSelector(std::vector<float> maxDistances) noexcept;
    [[nodiscard]] std::size_t update(float distance, float hysteresis) noexcept;
    [[nodiscard]] std::size_t current() const noexcept { return current_; }
    void reset() noexcept { current_ = 0; }
    [[nodiscard]] std::size_t count() const noexcept { return distances_.size(); }

private:
    std::vector<float> distances_;
    std::size_t current_ = 0;
};

// M17.2: polls authored asset files for changes so the game can hot-reload.
class AssetWatcher
{
public:
    AssetWatcher();
    ~AssetWatcher();
    AssetWatcher(AssetWatcher&&) noexcept;
    AssetWatcher& operator=(AssetWatcher&&) noexcept;
    AssetWatcher(const AssetWatcher&) = delete;
    AssetWatcher& operator=(const AssetWatcher&) = delete;

    bool watch(const std::string& path);
    [[nodiscard]] std::vector<std::string> poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] ToonMaterialDesc mapAssetMaterial(const MeshMaterialData& material);
[[nodiscard]] ToonMaterialDesc applyAssetMaterialVariant(const ToonMaterialDesc& base,
                                                         MaterialVariant variant);
[[nodiscard]] std::string_view materialVariantName(MaterialVariant variant) noexcept;

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
