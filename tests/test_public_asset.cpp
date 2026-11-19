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

TEST_CASE("Public Asset: LOD generation and watcher are exposed")
{
    MeshData mesh;
    for (int i = 0; i < 36; ++i)
    {
        MeshVertexData vertex;
        vertex.position = math::Vec3f{static_cast<float>(i % 6), static_cast<float>(i / 6), 0.0f};
        mesh.vertices.push_back(vertex);
        mesh.indices.push_back(static_cast<std::uint32_t>(i));
    }
    const std::vector<float> ratios{0.5f};
    auto levels = generateAssetLods(mesh, ratios);
    REQUIRE(levels.has_value());
    CHECK(levels->size() == 2);

    AssetLodSelector selector{{100.0f, 1000.0f, 1.0e9f}};
    CHECK(selector.update(50.0f, 20.0f) == 0);
    CHECK(selector.update(130.0f, 20.0f) == 1);

    AssetWatcher watcher;
    static_assert(std::is_move_constructible_v<AssetWatcher>);
    static_assert(!std::is_copy_constructible_v<AssetWatcher>);
}
