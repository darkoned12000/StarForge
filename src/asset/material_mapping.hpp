#pragma once

// M17.3: glTF PBR material -> M9.5 toon capability mapping, plus named
// material variants. Pure CPU (no Vulkan / engine math) so it is T1-testable.

#include "gltf_loader.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace starforge::asset
{

// Mirrors starforge::ToonCapability bit values so the mapping can be applied
// to Toon::Params.capabilities without a dependency on the public header.
enum class ToonCapabilityBit : std::uint32_t
{
    None = 0,
    Emissive = 1,
    Fresnel = 2,
    Faceted = 4,
    ScatteringRim = 8,
    ShadowDetail = 16,
    Textured = 32,
};

constexpr ToonCapabilityBit operator|(ToonCapabilityBit a, ToonCapabilityBit b) noexcept
{
    return static_cast<ToonCapabilityBit>(static_cast<std::uint32_t>(a) |
                                          static_cast<std::uint32_t>(b));
}

struct ToonMaterialDesc
{
    std::uint32_t capabilities = 0;
    std::array<float, 4> baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> shadowColor{0.10f, 0.14f, 0.24f, 1.0f};
    std::array<float, 4> midColor{0.6f, 0.4f, 0.4f, 1.0f};
    std::array<float, 4> emissiveColor{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> fresnelColor{0.3f, 0.7f, 1.0f, 1.0f};
    float bandCount = 3.0f;
    float rimStrength = 0.6f;
    float outlineWidth = 1.5f;
    float fresnelPower = 3.0f;
    bool doubleSided = false;
    std::string name;
};

// Maps a decoded glTF material to a toon material description. Deterministic
// and conservative: only well-understood PBR signals drive capability bits.
[[nodiscard]] ToonMaterialDesc mapMaterial(const MaterialData& material);

enum class MaterialVariant
{
    Default,
    Damaged,
    Shielded,
    Highlighted,
};

[[nodiscard]] const std::vector<std::string>& materialVariantNames();
[[nodiscard]] MaterialVariant materialVariant(std::size_t index) noexcept;
[[nodiscard]] std::string_view materialVariantName(MaterialVariant variant) noexcept;

// Applies a named variant on top of a base material (does not mutate it).
[[nodiscard]] ToonMaterialDesc
applyMaterialVariant(const ToonMaterialDesc& base, MaterialVariant variant);

} // namespace starforge::asset
