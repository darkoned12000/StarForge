// M17.7 (T1): authored hero ship / station glTF content loads correctly.
#include <doctest/doctest.h>

#include "asset/gltf_loader.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

GltfAsset loadAsset(const std::string& name)
{
    const auto path = assetPath(name);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE_MESSAGE(file.good(), "Missing authored asset %s", path.c_str());
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    auto asset = loadGlb(bytes.data(), bytes.size());
    REQUIRE(asset.has_value());
    return std::move(*asset);
}
} // namespace

TEST_CASE("M17.7: hero ship has inked multi-part armour and separate glowing cores")
{
    const auto asset = loadAsset("hero_ship.glb");
    REQUIRE(asset.meshes.size() == 6);
    CHECK(asset.meshes[0].name == "hero-hull");
    CHECK(asset.meshes[1].name == "hero-wings");
    CHECK(asset.meshes[2].name == "hero-canopy");
    CHECK(asset.meshes[3].name == "hero-engine-housings");
    CHECK(asset.meshes[4].name == "hero-engine-cores");
    CHECK(asset.meshes[5].name == "hero-nav-lights");

    for (std::size_t i = 0; i < asset.meshes.size(); ++i)
    {
        const auto& mesh = asset.meshes[i];
        CHECK_FALSE(mesh.vertices.empty());
        CHECK(mesh.indices.size() % 3 == 0);
        for (const auto& vertex : mesh.vertices)
            CHECK(vertex.uv[0] >= 0.0f);
        if (i == 5) continue; // nav lights are a flat emissive colour
        CHECK(mesh.material.hasBaseColorTexture);
        CHECK(mesh.material.baseColorTexture.width >= 128); // 256/128 inked textures
        CHECK(mesh.material.baseColorTexture.rgba.size() ==
              static_cast<std::size_t>(mesh.material.baseColorTexture.width) *
              mesh.material.baseColorTexture.height * 4u);
    }

    // Armour is opaque; only the recessed cores and nav beacons glow.
    CHECK(asset.meshes[3].material.emissiveFactor[0] == 0.0f);
    CHECK(asset.meshes[4].material.baseColorFactor[2] < 0.1f); // dark nozzle interiors
    CHECK(asset.meshes[2].material.roughnessFactor < 0.25f);  // glossy canopy
}

TEST_CASE("M17.7: station has a spire, lit habitat rings, trim and beacons")
{
    const auto asset = loadAsset("station.glb");
    REQUIRE(asset.meshes.size() == 4);
    CHECK(asset.meshes[0].name == "station-hub");
    CHECK(asset.meshes[1].name == "station-rings");
    CHECK(asset.meshes[2].name == "station-trim");
    CHECK(asset.meshes[3].name == "station-beacons");
    CHECK(asset.meshes[2].material.metallicFactor > 0.5f);    // gold trim
    // Windows use an emissive map sampled in the normal toon pass so they can
    // glow/bloom while preserving depth writes (no additive-pipeline artifacts).
    CHECK(asset.meshes[1].material.hasEmissiveTexture);
    CHECK(asset.meshes[1].material.emissiveFactor[0] > 0.0f);
    CHECK(asset.meshes[1].material.hasBaseColorTexture);
}
