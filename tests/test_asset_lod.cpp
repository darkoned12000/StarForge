// M17.4 (T1): meshoptimiser LOD generation and asset LOD selection.
#include <doctest/doctest.h>

#include "asset/lod_chain.hpp"
#include "asset/mesh_optimizer.hpp"

#include <cmath>
#include <vector>

using namespace starforge::asset;

namespace
{
MeshData gridMesh(std::size_t cells)
{
    MeshData mesh;
    const std::size_t side = cells + 1;
    for (std::size_t y = 0; y < side; ++y)
        for (std::size_t x = 0; x < side; ++x)
        {
            MeshVertexData vertex;
            vertex.position[0] = static_cast<float>(x);
            vertex.position[1] = static_cast<float>(y);
            vertex.position[2] = 0.0f;
            vertex.normal[2] = 1.0f;
            vertex.uv[0] = static_cast<float>(x) / static_cast<float>(cells);
            vertex.uv[1] = static_cast<float>(y) / static_cast<float>(cells);
            mesh.vertices.push_back(vertex);
        }
    for (std::size_t y = 0; y < cells; ++y)
        for (std::size_t x = 0; x < cells; ++x)
        {
            const auto a = static_cast<std::uint32_t>(y * side + x);
            const auto b = a + 1;
            const auto c = a + static_cast<std::uint32_t>(side);
            const auto d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    return mesh;
}
} // namespace

TEST_CASE("M17.4: mesh stats and optimisation preserve visible geometry")
{
    auto mesh = gridMesh(4);
    const auto before = meshStats(mesh);
    CHECK(mesh.vertices.size() == 25); // compact indexed source
    CHECK(before.triangles == 32);

    const auto after = optimizeMesh(mesh);
    CHECK(after.triangles == before.triangles); // no geometry lost
    CHECK(mesh.vertices.size() == before.triangles * 3); // expanded flat stream
    CHECK(mesh.indices.size() == before.triangles * 3);
    // Welded+optimised order is still a valid flat stream.
    for (std::size_t i = 0; i < mesh.indices.size(); ++i)
        CHECK(mesh.indices[i] == i);
}

TEST_CASE("M17.4: simplification produces smaller LOD levels")
{
    const auto base = gridMesh(8);
    CHECK(meshStats(base).triangles == 128);

    auto half = simplifyMesh(base, 0.5f);
    REQUIRE(half.has_value());
    CHECK(meshStats(*half).triangles < meshStats(base).triangles);

    auto quarter = simplifyMesh(base, 0.25f);
    REQUIRE(quarter.has_value());
    CHECK(meshStats(*quarter).triangles <= meshStats(*half).triangles);

    CHECK_FALSE(simplifyMesh(base, 0.0f).has_value());
    CHECK_FALSE(simplifyMesh(base, 1.5f).has_value());

    const std::vector<float> ratios{0.5f, 0.25f};
    const auto levels = generateLodLevels(base, ratios);
    REQUIRE(levels.has_value());
    REQUIRE(levels->size() == 3);
    CHECK(meshStats((*levels)[0]).triangles >= meshStats((*levels)[1]).triangles);
    CHECK(meshStats((*levels)[1]).triangles >= meshStats((*levels)[2]).triangles);
}

TEST_CASE("M17.4: LOD chain selects by distance with hysteresis")
{
    LodChain chain;
    chain.add(AssetHandle{1}, 0, 100.0f);
    chain.add(AssetHandle{2}, 0, 300.0f);
    chain.add(AssetHandle{3}, 0, 1000.0f);
    REQUIRE(chain.size() == 3);

    CHECK(chain.select(20.0f)->asset.id == 1);
    CHECK(chain.select(200.0f)->asset.id == 2);
    CHECK(chain.select(5000.0f)->asset.id == 3);

    LodSelector selector(chain);
    CHECK(selector.update(50.0f, 20.0f)->asset.id == 1);
    CHECK(selector.update(110.0f, 20.0f)->asset.id == 1); // inside hysteresis
    CHECK(selector.update(130.0f, 20.0f)->asset.id == 2); // crosses threshold
    CHECK(selector.update(90.0f, 20.0f)->asset.id == 2);  // inside hysteresis
    CHECK(selector.update(70.0f, 20.0f)->asset.id == 1);  // back to fine
}
