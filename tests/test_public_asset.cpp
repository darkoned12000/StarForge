// M17.3 (T3): public asset material mapping stays in the public surface.
#include <doctest/doctest.h>

#include "starforge/asset.hpp"

#include <type_traits>

using namespace starforge;

TEST_CASE("Public Asset: material mapping and variants are exposed")
{
    MeshMaterialData material;
    material.hasBaseColorTexture = true;
    material.metallicFactor = 0.9f;

    const auto desc = mapAssetMaterial(material);
    CHECK((desc.capabilities & static_cast<std::uint32_t>(AssetToonCapability::Textured)) != 0);
    CHECK((desc.capabilities & static_cast<std::uint32_t>(AssetToonCapability::Faceted)) != 0);

    const auto damaged = applyAssetMaterialVariant(desc, MaterialVariant::Damaged);
    CHECK((damaged.capabilities & static_cast<std::uint32_t>(AssetToonCapability::Emissive)) != 0);
    CHECK(materialVariantName(MaterialVariant::Shielded) == "Shielded");

    static_assert(std::is_move_constructible_v<Asset>);
    static_assert(!std::is_copy_constructible_v<Asset>);
}
