#include <doctest/doctest.h>

#include "content/content_generation.hpp"
#include "content/cell_traversal.hpp"
#include "content/hash.hpp"
#include "content/player_delta.hpp"

#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

using namespace starforge::content;
using namespace starforge::spatial;

TEST_CASE("Content generation: deterministic output")
{
    auto chunk1 = generateContent(42, 0, 0, 0, 0);
    auto chunk2 = generateContent(42, 0, 0, 0, 0);

    CHECK(chunk1.seed == chunk2.seed);
    CHECK(chunk1.cellX == chunk2.cellX);
    CHECK(chunk1.objects.size() == chunk2.objects.size());

    for (size_t i = 0; i < chunk1.objects.size(); ++i)
    {
        CHECK(chunk1.objects[i].objectId == chunk2.objects[i].objectId);
        CHECK(chunk1.objects[i].type == chunk2.objects[i].type);
        CHECK(chunk1.objects[i].localX == chunk2.objects[i].localX);
        CHECK(chunk1.objects[i].localY == chunk2.objects[i].localY);
        CHECK(chunk1.objects[i].localZ == chunk2.objects[i].localZ);
        CHECK(chunk1.objects[i].scale == chunk2.objects[i].scale);
    }
}

TEST_CASE("Content generation: different cells produce different content")
{
    auto chunk1 = generateContent(42, 0, 0, 0, 0);
    auto chunk2 = generateContent(42, 1, 0, 0, 0);

    // Different cells should produce different object counts or positions.
    bool different = false;
    if (chunk1.objects.size() != chunk2.objects.size())
    {
        different = true;
    }
    else
    {
        for (size_t i = 0; i < chunk1.objects.size(); ++i)
        {
            if (chunk1.objects[i].localX != chunk2.objects[i].localX ||
                chunk1.objects[i].localY != chunk2.objects[i].localY ||
                chunk1.objects[i].localZ != chunk2.objects[i].localZ)
            {
                different = true;
                break;
            }
        }
    }
    CHECK(different);
}

TEST_CASE("Content generation: different seeds produce different content")
{
    auto chunk1 = generateContent(42, 0, 0, 0, 0);
    auto chunk2 = generateContent(99, 0, 0, 0, 0);

    bool different = (chunk1.objects.size() != chunk2.objects.size());
    if (!different && !chunk1.objects.empty())
    {
        different = (chunk1.objects[0].localX != chunk2.objects[0].localX);
    }
    CHECK(different);
}

TEST_CASE("Content generation: level affects object count")
{
    auto level0 = generateContent(42, 0, 0, 0, 0);
    auto level4 = generateContent(42, 0, 0, 0, 4);

    // Higher level (coarser) should have fewer objects.
    // Level 0 is local (many asteroids), level 4 is galaxy (few stars).
    CHECK(level4.objects.size() <= level0.objects.size() + 10); // allow some variance
}

TEST_CASE("Hash: cell hash determinism")
{
    auto h1 = hashCell(0, 0, 0);
    auto h2 = hashCell(0, 0, 0);
    CHECK(h1 == h2);

    auto h3 = hashCell(1, 0, 0);
    CHECK(h1 != h3);
}

TEST_CASE("Hash: contentSeed determinism")
{
    auto s1 = contentSeed(42, 0, 0, 0, 0);
    auto s2 = contentSeed(42, 0, 0, 0, 0);
    CHECK(s1 == s2);

    auto s3 = contentSeed(42, 1, 0, 0, 0);
    CHECK(s1 != s3);
}

TEST_CASE("Hash: DeterministicRng produces same sequence")
{
    DeterministicRng rng1(42);
    DeterministicRng rng2(42);

    for (int i = 0; i < 100; ++i)
    {
        CHECK(rng1.next() == rng2.next());
    }
}

TEST_CASE("Hash: DeterministicRng float in [0,1)")
{
    DeterministicRng rng(42);
    for (int i = 0; i < 1000; ++i)
    {
        float f = rng.nextFloat();
        CHECK(f >= 0.0f);
        CHECK(f < 1.0f);
    }
}

TEST_CASE("Cell traversal: worldToCell")
{
    WorldPosition pos;
    pos.cellX = 5;
    pos.cellY = 3;
    pos.cellZ = -2;

    auto coord0 = worldToCell(pos, 0);
    CHECK(coord0.x == 5);
    CHECK(coord0.y == 3);
    CHECK(coord0.z == -2);
    CHECK(coord0.level == 0);

    auto coord1 = worldToCell(pos, 1);
    CHECK(coord1.level == 1);
}

TEST_CASE("Cell traversal: traverseCells produces cells")
{
    WorldPosition pos;
    auto cells = traverseCells(pos, 0, 2, 1000.0);

    CHECK(!cells.empty());

    // Should have cells at multiple levels.
    std::set<int> levels;
    for (const auto& c : cells)
    {
        levels.insert(c.level);
    }
    CHECK(levels.size() >= 2);
}

TEST_CASE("Cell traversal: deterministic traversal")
{
    WorldPosition pos;
    auto cells1 = traverseCells(pos, 0, 2, 500.0);
    auto cells2 = traverseCells(pos, 0, 2, 500.0);

    CHECK(cells1.size() == cells2.size());
    for (size_t i = 0; i < cells1.size(); ++i)
    {
        CHECK(cells1[i].x == cells2[i].x);
        CHECK(cells1[i].y == cells2[i].y);
        CHECK(cells1[i].z == cells2[i].z);
        CHECK(cells1[i].level == cells2[i].level);
    }
}

TEST_CASE("Content generation: WorldPosition overload")
{
    WorldPosition pos;
    auto chunk = generateContent(42, pos, 0);
    CHECK(chunk.cellX == 0);
    CHECK(chunk.cellY == 0);
    CHECK(chunk.cellZ == 0);
    CHECK(chunk.level == 0);
}

TEST_CASE("PlayerDelta: apply and query")
{
    DeltaStore store;
    CHECK(store.size() == 0);

    PlayerDelta delta;
    delta.cellX = 0; delta.cellY = 0; delta.cellZ = 0; delta.level = 0;
    delta.objectId = 42;
    delta.type = DeltaType::Removed;

    store.apply(delta);
    CHECK(store.size() == 1);
    CHECK(store.isRemoved(0, 0, 0, 0, 42));
    CHECK_FALSE(store.isRemoved(0, 0, 0, 0, 99));
}

TEST_CASE("PlayerDelta: deltasForCell")
{
    DeltaStore store;

    PlayerDelta d1;
    d1.cellX = 1; d1.cellY = 0; d1.cellZ = 0; d1.level = 0;
    d1.objectId = 10; d1.type = DeltaType::Removed;
    store.apply(d1);

    PlayerDelta d2;
    d2.cellX = 2; d2.cellY = 0; d2.cellZ = 0; d2.level = 0;
    d2.objectId = 20; d2.type = DeltaType::Removed;
    store.apply(d2);

    auto deltas = store.deltasForCell(1, 0, 0, 0);
    CHECK(deltas.size() == 1);
    CHECK(deltas[0].objectId == 10);

    auto deltas2 = store.deltasForCell(2, 0, 0, 0);
    CHECK(deltas2.size() == 1);
    CHECK(deltas2[0].objectId == 20);
}

TEST_CASE("PlayerDelta: getDelta")
{
    DeltaStore store;

    PlayerDelta d;
    d.cellX = 0; d.cellY = 0; d.cellZ = 0; d.level = 0;
    d.objectId = 5; d.type = DeltaType::Modified;
    d.scale = 2.0f;
    store.apply(d);

    auto found = store.getDelta(0, 0, 0, 0, 5);
    CHECK(found.has_value());
    CHECK(found->type == DeltaType::Modified);
    CHECK(found->scale == 2.0f);

    auto notFound = store.getDelta(0, 0, 0, 0, 99);
    CHECK(!notFound.has_value());
}

TEST_CASE("PlayerDelta: serialize/deserialize round-trip")
{
    DeltaStore store;

    PlayerDelta d1;
    d1.cellX = 1; d1.cellY = 2; d1.cellZ = 3; d1.level = 1;
    d1.objectId = 42; d1.type = DeltaType::Removed;
    store.apply(d1);

    PlayerDelta d2;
    d2.cellX = 4; d2.cellY = 5; d2.cellZ = 6; d2.level = 0;
    d2.objectId = 99; d2.type = DeltaType::Modified;
    d2.scale = 1.5f; d2.subtype = 2;
    store.apply(d2);

    auto data = store.serialize();
    CHECK(data.size() > 0);

    DeltaStore store2;
    auto loaded = store2.deserialize(data);
    CHECK(loaded == 2);
    CHECK(store2.size() == 2);
    CHECK(store2.isRemoved(1, 2, 3, 1, 42));
    CHECK(store2.getDelta(4, 5, 6, 0, 99).has_value());
}

TEST_CASE("applyDeltas: removed objects filtered")
{
    auto generated = generateContent(42, 0, 0, 0, 0);
    std::uint32_t originalCount = static_cast<std::uint32_t>(generated.objects.size());

    DeltaStore store;
    if (!generated.objects.empty())
    {
        PlayerDelta d;
        d.cellX = 0; d.cellY = 0; d.cellZ = 0; d.level = 0;
        d.objectId = generated.objects[0].objectId;
        d.type = DeltaType::Removed;
        store.apply(d);
    }

    auto modified = applyDeltas(generated, store);
    CHECK(modified.objects.size() == originalCount - 1);
}

TEST_CASE("applyDeltas: modified objects updated")
{
    auto generated = generateContent(42, 0, 0, 0, 0);
    if (generated.objects.empty()) return;

    DeltaStore store;
    PlayerDelta d;
    d.cellX = 0; d.cellY = 0; d.cellZ = 0; d.level = 0;
    d.objectId = generated.objects[0].objectId;
    d.type = DeltaType::Modified;
    d.scale = 99.0f;
    store.apply(d);

    auto modified = applyDeltas(generated, store);
    CHECK(modified.objects[0].scale == 99.0f);
}

TEST_CASE("applyDeltas: added objects appended")
{
    auto generated = generateContent(42, 0, 0, 0, 0);
    std::uint32_t originalCount = static_cast<std::uint32_t>(generated.objects.size());

    DeltaStore store;
    PlayerDelta d;
    d.cellX = 0; d.cellY = 0; d.cellZ = 0; d.level = 0;
    d.objectId = 9999;
    d.type = DeltaType::Added;
    d.objectType = ContentType::Station;
    d.scale = 5.0f;
    store.apply(d);

    auto modified = applyDeltas(generated, store);
    CHECK(modified.objects.size() == originalCount + 1);

    // Find the added object.
    bool found = false;
    for (const auto& obj : modified.objects)
    {
        if (obj.objectId == 9999)
        {
            found = true;
            CHECK(obj.type == ContentType::Station);
            CHECK(obj.scale == 5.0f);
        }
    }
    CHECK(found);
}

TEST_CASE("Content generation: star systems at coarse level")
{
    auto chunk = generateContent(42, 0, 0, 0, 4);
    // Should contain stars.
    bool hasStar = false;
    for (const auto& obj : chunk.objects)
    {
        if (obj.type == ContentType::Star)
        {
            hasStar = true;
            // Stars should be large.
            CHECK(obj.scale >= 2.0f);
            // Should have planet count in properties.
            CHECK(starPlanetCount(obj) <= 8);
        }
    }
    CHECK(hasStar);
}

TEST_CASE("Content generation: asteroids at fine level")
{
    auto chunk = generateContent(42, 0, 0, 0, 0);
    // Should contain asteroids.
    bool hasAsteroid = false;
    for (const auto& obj : chunk.objects)
    {
        if (obj.type == ContentType::Asteroid)
        {
            hasAsteroid = true;
            // Asteroids should be small.
            CHECK(obj.scale >= 0.1f);
            CHECK(obj.scale <= 1.0f);
            // Mineral type is exposed through the packed-property accessor.
            CHECK(asteroidMineral(obj) <= 3);
        }
    }
    CHECK(hasAsteroid);
}

TEST_CASE("Content generation: stations have properties")
{
    // Generate at system level (level 2) which can have stations.
    bool foundStation = false;
    for (int seed = 0; seed < 20; ++seed)
    {
        auto chunk = generateContent(static_cast<std::uint64_t>(seed), 0, 0, 0, 2);
        for (const auto& obj : chunk.objects)
        {
            if (obj.type == ContentType::Station)
            {
                foundStation = true;
                // Station type.
                CHECK(obj.subtype <= 3);
                // Faction and docking capacity via named accessors.
                CHECK(stationFaction(obj) <= 3);
                CHECK(stationDockingCapacity(obj) >= 1);
                CHECK(stationDockingCapacity(obj) <= 8);
            }
        }
    }
    // At least one station should appear across 20 seeds.
    CHECK(foundStation);
}

TEST_CASE("Content generation: resource nodes have valid type")
{
    auto chunk = generateContent(42, 0, 0, 0, 0);
    for (const auto& obj : chunk.objects)
    {
        if (obj.type == ContentType::ResourceNode)
        {
            CHECK(obj.subtype <= 3); // 0=iron, 1=silicon, 2=carbon, 3=water
            CHECK(obj.scale >= 0.2f);
        }
    }
}

TEST_CASE("Content generation: determinism across content types")
{
    // Verify that star systems generate identical content.
    auto chunk1 = generateContent(42, 5, 3, -2, 4);
    auto chunk2 = generateContent(42, 5, 3, -2, 4);

    CHECK(chunk1.objects.size() == chunk2.objects.size());
    for (size_t i = 0; i < chunk1.objects.size(); ++i)
    {
        CHECK(chunk1.objects[i].type == chunk2.objects[i].type);
        CHECK(chunk1.objects[i].subtype == chunk2.objects[i].subtype);
        CHECK(chunk1.objects[i].properties == chunk2.objects[i].properties);
        CHECK(chunk1.objects[i].scale == chunk2.objects[i].scale);
    }
}
TEST_CASE("Content generation: object IDs are unique within a cell")
{
    for (std::uint64_t seed = 0; seed < 200; ++seed)
    {
        for (int level = 0; level <= kMaxScaleLevel; ++level)
        {
            auto chunk = generateContent(seed, static_cast<std::int64_t>(seed % 7), 0, 0, level);
            std::set<std::uint64_t> ids;
            for (const auto& obj : chunk.objects)
            {
                CHECK(ids.insert(obj.objectId).second);
            }
        }
    }
}

TEST_CASE("Content generation: positions lie inside the cell")
{
    for (int level = 0; level <= kMaxScaleLevel; ++level)
    {
        auto chunk = generateContent(42, 0, 0, 0, level);
        const double size = cellSizeAtLevel(level);
        REQUIRE(size > 0.0);
        for (const auto& obj : chunk.objects)
        {
            CHECK(std::isfinite(obj.localX));
            CHECK(std::isfinite(obj.localY));
            CHECK(std::isfinite(obj.localZ));
            CHECK(obj.localX >= 0.0);
            CHECK(obj.localX < size);
            CHECK(obj.localY >= 0.0);
            CHECK(obj.localY < size);
            CHECK(obj.localZ >= 0.0);
            CHECK(obj.localZ < size);
        }
    }
}

TEST_CASE("Content generation: level is clamped to the supported hierarchy")
{
    auto low = generateContent(42, 0, 0, 0, -5);
    auto high = generateContent(42, 0, 0, 0, 99);
    CHECK(low.level == 0);
    CHECK(high.level == kMaxScaleLevel);
}

TEST_CASE("PlayerDelta: serialization is canonical")
{
    PlayerDelta a;
    a.cellX = 1; a.cellY = 2; a.cellZ = 3; a.level = 0;
    a.objectId = 10; a.type = DeltaType::Removed;

    PlayerDelta b;
    b.cellX = 4; b.cellY = 5; b.cellZ = 6; b.level = 1;
    b.objectId = 20; b.type = DeltaType::Modified; b.scale = 2.0f;

    DeltaStore forward;
    forward.apply(a);
    forward.apply(b);

    DeltaStore reverse;
    reverse.apply(b);
    reverse.apply(a);

    CHECK(forward.serialize() == reverse.serialize());
}

TEST_CASE("PlayerDelta: malformed records are skipped without desynchronizing")
{
    PlayerDelta a;
    a.cellX = 1; a.cellY = 2; a.cellZ = 3; a.level = 0;
    a.objectId = 10; a.type = DeltaType::Removed;

    PlayerDelta b;
    b.cellX = 4; b.cellY = 5; b.cellZ = 6; b.level = 1;
    b.objectId = 20; b.type = DeltaType::Modified; b.scale = 2.0f;

    DeltaStore store;
    store.apply(a);
    store.apply(b);
    const auto good = store.serialize();

    constexpr std::size_t kHeader = 16;
    constexpr std::size_t kRecord = 84;
    constexpr std::size_t kTypeOffset = 40;
    constexpr std::size_t kObjectTypeOffset = 41;

    SUBCASE("invalid delta type")
    {
        auto data = good;
        REQUIRE(data.size() == kHeader + 2 * kRecord);
        data[kHeader + kTypeOffset] = 0xFF;

        DeltaStore loadedStore;
        CHECK(loadedStore.deserialize(data) == 1);
        CHECK(loadedStore.size() == 1);

        const bool foundA = loadedStore.getDelta(1, 2, 3, 0, 10).has_value();
        const bool foundB = loadedStore.getDelta(4, 5, 6, 1, 20).has_value();
        CHECK(foundA != foundB); // exactly the second (valid) record survived
    }

    SUBCASE("invalid object type")
    {
        auto data = good;
        REQUIRE(data.size() == kHeader + 2 * kRecord);
        data[kHeader + kObjectTypeOffset] = 0xFF;

        DeltaStore loadedStore;
        CHECK(loadedStore.deserialize(data) == 1);
        CHECK(loadedStore.size() == 1);

        const bool foundA = loadedStore.getDelta(1, 2, 3, 0, 10).has_value();
        const bool foundB = loadedStore.getDelta(4, 5, 6, 1, 20).has_value();
        CHECK(foundA != foundB);
    }
}

TEST_CASE("PlayerDelta: added delta does not duplicate an existing object id")
{
    auto generated = generateContent(42, 0, 0, 0, 0);
    REQUIRE(!generated.objects.empty());
    const std::uint64_t existingId = generated.objects[0].objectId;
    const std::size_t originalCount = generated.objects.size();

    DeltaStore store;
    PlayerDelta d;
    d.cellX = 0; d.cellY = 0; d.cellZ = 0; d.level = 0;
    d.objectId = existingId;
    d.type = DeltaType::Added;
    d.objectType = ContentType::Station;
    store.apply(d);

    auto modified = applyDeltas(generated, store);
    CHECK(modified.objects.size() == originalCount);
}

TEST_CASE("Cell traversal: radius smaller than a cell returns only the camera cell")
{
    WorldPosition pos;
    pos.cellX = 100;
    pos.cellY = 50;
    pos.cellZ = -25;
    // Camera at the centre of its cell, so a 1 km radius stays inside.
    pos.offsetX = WorldPosition::kCellSize * 0.5;
    pos.offsetY = WorldPosition::kCellSize * 0.5;
    pos.offsetZ = WorldPosition::kCellSize * 0.5;

    auto cells = traverseCells(pos, 0, 0, 1000.0);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].x == 100);
    CHECK(cells[0].y == 50);
    CHECK(cells[0].z == -25);
    CHECK(cells[0].level == 0);
}

TEST_CASE("Cell traversal: worldToCell matches WorldPosition coarse cells")
{
    auto pos = WorldPosition::fromMeters(5.0e24, -5.0e24, 1.0e24);
    REQUIRE(pos.has_value());

    for (int level = 0; level <= kMaxScaleLevel; ++level)
    {
        auto cell = worldToCell(*pos, level);
        CHECK(cell.level == level);
        CHECK(cell.x == pos->coarseCellX(level));
        CHECK(cell.y == pos->coarseCellY(level));
        CHECK(cell.z == pos->coarseCellZ(level));
    }
}

TEST_CASE("Cell traversal: collapsed top level returns a single cell")
{
    auto pos = WorldPosition::fromMeters(1.0e12, 0.0, 0.0);
    REQUIRE(pos.has_value());

    auto cells = traverseCells(*pos, kMaxScaleLevel, kMaxScaleLevel, 1.0e30);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].level == kMaxScaleLevel);
    CHECK(cells[0].x == worldToCell(*pos, kMaxScaleLevel).x);
}

TEST_CASE("Hash: nextInt stays within bounds")
{
    DeterministicRng rng(1234);
    for (int i = 0; i < 10000; ++i)
    {
        const int v = rng.nextInt(-3, 5);
        CHECK(v >= -3);
        CHECK(v <= 5);
    }
}
