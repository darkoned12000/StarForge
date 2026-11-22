#include "material_mapping.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::asset
{

namespace
{
void scale(std::array<float, 4>& color, float factor) noexcept
{
    for (std::size_t i = 0; i < 3; ++i) color[i] = std::clamp(color[i] * factor, 0.0f, 1.0f);
}
} // namespace

ToonMaterialDesc mapMaterial(const MaterialData& material)
{
    ToonMaterialDesc desc;
    desc.name = material.name;
    desc.doubleSided = material.doubleSided;
    for (std::size_t i = 0; i < 4; ++i)
    {
        desc.baseColor[i] = material.baseColorFactor[i];
        desc.emissiveColor[i] = material.emissiveFactor[i];
    }
    desc.emissiveColor[3] = 1.0f;

    std::uint32_t caps = 0;
    if (material.hasBaseColorTexture)
        caps |= static_cast<std::uint32_t>(ToonCapabilityBit::Textured);
    // Emissive capability drives the additive glow pipeline. Pure emissive-color
    // materials use additive blending; emissive-textured opaque surfaces are
    // sampled in the normal toon pass to preserve depth writes/culling.
    const bool hasEmissiveOutput = material.emissiveFactor[0] > 0.0f ||
                                   material.emissiveFactor[1] > 0.0f ||
                                   material.emissiveFactor[2] > 0.0f;
    const bool hasEmissiveMap = material.hasEmissiveTexture;
    if (hasEmissiveOutput && !hasEmissiveMap)
        caps |= static_cast<std::uint32_t>(ToonCapabilityBit::Emissive);
    if (material.metallicFactor >= 0.5f)
        caps |= static_cast<std::uint32_t>(ToonCapabilityBit::Faceted);
    if (material.roughnessFactor <= 0.35f)
        caps |= static_cast<std::uint32_t>(ToonCapabilityBit::ScatteringRim);
    if (material.hasNormalTexture || material.hasMetallicRoughnessTexture ||
        material.hasOcclusionTexture)
        caps |= static_cast<std::uint32_t>(ToonCapabilityBit::ShadowDetail);
    if (material.alphaMode == "BLEND")
        caps |= static_cast<std::uint32_t>(ToonCapabilityBit::Fresnel);
    desc.capabilities = caps;

    // Toon ramp responds to surface roughness; a few broad bands keep the
    // cel-shaded look instead of approximating PBR continuously.
    desc.bandCount = material.roughnessFactor >= 0.75f ? 2.0f
                   : material.roughnessFactor <= 0.35f ? 4.0f
                                                        : 3.0f;
    desc.rimStrength = material.metallicFactor >= 0.5f ? 0.9f : 0.6f;
    // Interior panel seams are painted into the albedo, so the outer hull
    // outline stays thin to match the inked line weight.
    desc.outlineWidth = material.doubleSided ? 1.2f : 1.5f;
    return desc;
}

const std::vector<std::string>& materialVariantNames()
{
    static const std::vector<std::string> names = {"Default", "Damaged", "Shielded", "Highlighted"};
    return names;
}

MaterialVariant materialVariant(std::size_t index) noexcept
{
    switch (index)
    {
    case 1: return MaterialVariant::Damaged;
    case 2: return MaterialVariant::Shielded;
    case 3: return MaterialVariant::Highlighted;
    default: return MaterialVariant::Default;
    }
}

std::string_view materialVariantName(MaterialVariant variant) noexcept
{
    const auto index = static_cast<std::size_t>(variant);
    const auto& names = materialVariantNames();
    return index < names.size() ? std::string_view(names[index]) : std::string_view("Default");
}

ToonMaterialDesc applyMaterialVariant(const ToonMaterialDesc& base, MaterialVariant variant)
{
    ToonMaterialDesc result = base;
    switch (variant)
    {
    case MaterialVariant::Damaged:
        // Stay on the opaque toon pipeline: capability 1 switches to the
        // additive glow pass (no depth write), which would make the hull look
        // transparent. Tint via opaque colours instead.
        scale(result.baseColor, 0.55f);
        result.baseColor[0] = std::clamp(result.baseColor[0] + 0.25f, 0.0f, 1.0f);
        result.shadowColor = {0.25f, 0.06f, 0.04f, 1.0f};
        result.name += " (Damaged)";
        break;
    case MaterialVariant::Shielded:
        result.capabilities |= static_cast<std::uint32_t>(ToonCapabilityBit::Fresnel);
        result.fresnelColor = {0.2f, 0.9f, 0.7f, 1.0f};
        result.fresnelPower = 2.4f;
        result.name += " (Shielded)";
        break;
    case MaterialVariant::Highlighted:
        // Brighten the opaque surface + rim; do NOT enable additive emissive
        // (that pipeline skips depth writes and exposes geometry behind the
        // ship). The emissive bit is reserved for genuinely glowing parts.
        result.baseColor[0] = std::clamp(result.baseColor[0] + 0.15f, 0.0f, 1.0f);
        result.baseColor[1] = std::clamp(result.baseColor[1] + 0.12f, 0.0f, 1.0f);
        result.midColor = {0.9f, 0.8f, 0.35f, 1.0f};
        result.rimStrength = 1.0f;
        result.bandCount = 4.0f;
        result.name += " (Highlighted)";
        break;
    case MaterialVariant::Default:
    default:
        break;
    }
    return result;
}

} // namespace starforge::asset
