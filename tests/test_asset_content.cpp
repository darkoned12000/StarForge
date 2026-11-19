// M17.7 (T1): authored hero ship / station glTF content loads correctly.
#include <doctest/doctest.h>

#include "asset/gltf_loader.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#ifndef STARFORGE_ASSET_DIR
#define STARFORGE_ASSET_DIR "assets/models"
#endif

using namespace starforge::asset;

namespace
{
std::filesystem::path assetPath(const std::string& name)
{
    return std::filesystem::path(STARFORGE_ASSET_DIR) / name;
}
} // namespace

TEST_CASE("M17.7: hero ship has textured hull, wings and emissive engines")
{
    const auto path = assetPath("hero_ship.glb");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE_MESSAGE(file.good(), "Missing authored asset %s", path.c_str());
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));

    auto asset = loadGlb(bytes.data(), bytes.size());
    REQUIRE(asset.has_value());
    REQUIRE(asset->meshes.size() == 3);
    CHECK(asset->meshes[0].name == "hero-hull");
    CHECK(asset->meshes[1].name == "hero-wings");
    CHECK(asset->meshes[2].name == "hero-engines");

    for (const auto& mesh : asset->meshes)
    {
        CHECK_FALSE(mesh.vertices.empty());
        CHECK(mesh.indices.size() % 3 == 0);
        CHECK(mesh.material.hasBaseColorTexture);
        CHECK(mesh.material.baseColorTexture.width == 8);
        CHECK(mesh.material.baseColorTexture.rgba.size() == 8u * 8u * 4u);
        for (const auto& vertex : mesh.vertices)
            CHECK(vertex.uv[0] >= 0.0f);
    }
    CHECK(asset->meshes[2].material.emissiveFactor[0] > 0.5f);
}

TEST_CASE("M17.7: station has a hub, ring and arms with material variants")
{
    const auto path = assetPath("station.glb");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE_MESSAGE(file.good(), "Missing authored asset %s", path.c_str());
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));

    auto asset = loadGlb(bytes.data(), bytes.size());
    REQUIRE(asset.has_value());
    REQUIRE(asset->meshes.size() == 3);
    CHECK(asset->meshes[0].name == "station-hub");
    CHECK(asset->meshes[1].name == "station-ring");
    CHECK(asset->meshes[2].name == "station-arms");
    CHECK(asset->meshes[1].material.metallicFactor > 0.5f);
}
