// M17.3 (T1): glTF PBR -> toon capability mapping and material variants.
#include <doctest/doctest.h>

#include "asset/material_mapping.hpp"

using namespace starforge::asset;

namespace
{
constexpr std::uint32_t bit(ToonCapabilityBit c) noexcept
{
    return static_cast<std::uint32_t>(c);
}
} // namespace

TEST_CASE("M17.3: base colour + texture map to the textured toon capability")
{
    MaterialData material;
    material.name = "hull";
    material.baseColorFactor[0] = 0.8f;
    material.baseColorFactor[1] = 0.2f;
    material.roughnessFactor = 0.5f;
    material.hasBaseColorTexture = true;

    const auto desc = mapMaterial(material);
    CHECK(desc.name == "hull");
    CHECK(desc.baseColor[0] == doctest::Approx(0.8f));
    CHECK((desc.capabilities & bit(ToonCapabilityBit::Textured)) != 0);
    CHECK((desc.capabilities & bit(ToonCapabilityBit::Emissive)) == 0);
    CHECK(desc.bandCount == 3.0f);
}

TEST_CASE("M17.3: PBR signals map to distinct toon capabilities")
{
    MaterialData metal;
    metal.metallicFactor = 0.8f;
    metal.roughnessFactor = 0.2f;
    metal.hasNormalTexture = true;
    metal.emissiveFactor[1] = 0.5f;
    metal.alphaMode = "BLEND";
    const auto desc = mapMaterial(metal);
    CHECK((desc.capabilities & bit(ToonCapabilityBit::Faceted)) != 0);
    CHECK((desc.capabilities & bit(ToonCapabilityBit::ScatteringRim)) != 0);
    CHECK((desc.capabilities & bit(ToonCapabilityBit::ShadowDetail)) != 0);
    CHECK((desc.capabilities & bit(ToonCapabilityBit::Emissive)) != 0);
    CHECK((desc.capabilities & bit(ToonCapabilityBit::Fresnel)) != 0);
    CHECK(desc.bandCount == 4.0f);

    MaterialData matte;
    matte.roughnessFactor = 0.9f;
    CHECK(mapMaterial(matte).bandCount == 2.0f);
}

TEST_CASE("M17.3: variants layer capabilities without mutating the base")
{
    MaterialData material;
    material.hasBaseColorTexture = true;
    const auto base = mapMaterial(material);

    const auto damaged = applyMaterialVariant(base, MaterialVariant::Damaged);
    // Variants must never turn on additive emissive for whole hulls: that
    // pipeline skips depth writes and makes the ship look transparent.
    CHECK((damaged.capabilities & bit(ToonCapabilityBit::Emissive)) == 0);
    CHECK(damaged.shadowColor[0] > base.shadowColor[0]);
    const bool darkened = damaged.baseColor[0] < base.baseColor[0] ||
                          damaged.baseColor[1] < base.baseColor[1];
    CHECK(darkened);

    const auto shielded = applyMaterialVariant(base, MaterialVariant::Shielded);
    CHECK((shielded.capabilities & bit(ToonCapabilityBit::Fresnel)) != 0);

    const auto highlighted = applyMaterialVariant(base, MaterialVariant::Highlighted);
    CHECK((highlighted.capabilities & bit(ToonCapabilityBit::Emissive)) == 0);
    CHECK(highlighted.rimStrength > base.rimStrength);
    CHECK(highlighted.bandCount > base.bandCount);

    CHECK(materialVariantNames().size() == 4);
    CHECK(materialVariant(99) == MaterialVariant::Default);
    CHECK(materialVariantName(MaterialVariant::Damaged) == "Damaged");
}
