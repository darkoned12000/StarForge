// M17.3 (T3): public asset material mapping stays in the public surface.
#include <doctest/doctest.h>

#include "starforge/asset.hpp"

#include <type_traits>
#include <vector>

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

TEST_CASE("Public Asset: streamed textures expose mip residency")
{
    std::vector<std::uint8_t> pixels(4 * 4 * 4, 200);
    auto texture = StreamingTexture::fromRGBA(4, 4, pixels, 80);
    REQUIRE(texture.has_value());
    CHECK(texture->mipCount() == 3);
    CHECK_FALSE(texture->resident(1));
    CHECK(texture->request(1));
    CHECK(texture->resident(1));
    CHECK(texture->data(1).size() == 4 * 4); // 2x2 RGBA8
    CHECK(texture->width(1) == 2);
    CHECK(texture->residentBytes() == 4 * 4);
    CHECK(texture->selectMip(1) == 2);
    static_assert(std::is_move_constructible_v<StreamingTexture>);
    static_assert(!std::is_copy_constructible_v<StreamingTexture>);
}
