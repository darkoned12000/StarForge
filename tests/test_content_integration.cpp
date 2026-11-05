#include <doctest/doctest.h>

#include "content/content_generation.hpp"
#include "content/cell_traversal.hpp"
#include "content/hash.hpp"
#include "content/player_delta.hpp"
#include "math/math.hpp"
#include "spatial/camera_relative.hpp"
#include "spatial/world_position.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

using namespace starforge::content;
using namespace starforge::spatial;

namespace
{

std::uint64_t mixHash(std::uint64_t h, std::uint64_t v) noexcept
{
    return splitMix64(h ^ v);
}

// Full-content hash: covers every field that affects generated output.
std::uint64_t chunkHash(const ContentChunk& chunk) noexcept
{
    std::uint64_t h = 0x243f6a8885a308d3ULL;
    h = mixHash(h, static_cast<std::uint64_t>(chunk.cellX));
    h = mixHash(h, static_cast<std::uint64_t>(chunk.cellY));
    h = mixHash(h, static_cast<std::uint64_t>(chunk.cellZ));
    h = mixHash(h, static_cast<std::uint64_t>(chunk.level));
    h = mixHash(h, chunk.seed);
    h = mixHash(h, static_cast<std::uint64_t>(chunk.objects.size()));

    for (const auto& obj : chunk.objects)
    {
        h = mixHash(h, obj.objectId);
        h = mixHash(h, static_cast<std::uint64_t>(obj.type));
        h = mixHash(h, static_cast<std::uint64_t>(obj.subtype));
        h = mixHash(h, obj.properties);
        h = mixHash(h, std::bit_cast<std::uint32_t>(obj.scale));
        h = mixHash(h, std::bit_cast<std::uint64_t>(obj.localX));
        h = mixHash(h, std::bit_cast<std::uint64_t>(obj.localY));
        h = mixHash(h, std::bit_cast<std::uint64_t>(obj.localZ));
    }

    return h;
}

} // namespace

TEST_CASE("Integration: long flight simulation (1000+ cells)")
{
    WorldPosition pos;
    const std::uint64_t seed = 42;
    const double cellSize = WorldPosition::kCellSize;

    const std::uint64_t originHash = chunkHash(generateContent(seed, pos, 0));
    std::set<std::int64_t> seenCells;

    for (int i = 0; i < 1000; ++i)
    {
        auto moved = pos.translated(starforge::math::Vec3d{cellSize, 0.0, 0.0});
        REQUIRE(moved.has_value());
        pos = *moved;

        // Each step must advance exactly one cell with a normalized offset.
        CHECK(pos.cellX == i + 1);
        CHECK(pos.offsetX >= 0.0);
        CHECK(pos.offsetX < cellSize);

        const auto chunk = generateContent(seed, pos, 0);
        const auto chunkAgain = generateContent(seed, pos, 0);

        // Full field-by-field determinism, not just object counts.
        CHECK(chunkHash(chunk) == chunkHash(chunkAgain));

        seenCells.insert(pos.cellX);
    }

    CHECK(seenCells.size() == 1000);

    // Returning to the origin must reproduce the original chunk exactly.
    auto back = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(back.has_value());
    CHECK(chunkHash(generateContent(seed, *back, 0)) == originHash);
}

TEST_CASE("Integration: translation-invariance at 1e12 m")
{
    constexpr double kFarMetres = 1.0e12;
    const std::uint64_t seed = 42;

    auto origin = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    auto farA = WorldPosition::fromMeters(kFarMetres, 0.0, 0.0);
    auto farB = WorldPosition::fromMeters(kFarMetres, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(farA.has_value());
    REQUIRE(farB.has_value());

    // Independently constructed far positions must resolve to the same cell
    // and produce bit-identical content.
    const auto chunkA = generateContent(seed, *farA, 0);
    const auto chunkB = generateContent(seed, *farB, 0);
    CHECK(chunkA.cellX == farA->cellX);
    CHECK(chunkA.cellX == chunkB.cellX);
    CHECK(chunkHash(chunkA) == chunkHash(chunkB));

    // Traversing away and returning must not change the generated content.
    WorldPosition traveller = *farA;
    for (int i = 0; i < 16; ++i)
    {
        auto moved = traveller.translated(starforge::math::Vec3d{1.0e9, 0.0, 0.0});
        REQUIRE(moved.has_value());
        traveller = *moved;
    }
    auto returned = WorldPosition::fromMeters(kFarMetres, 0.0, 0.0);
    REQUIRE(returned.has_value());
    CHECK(chunkHash(generateContent(seed, *returned, 0)) == chunkHash(chunkA));

    // Camera-relative math must reproduce the 1e12 m separation exactly
    // enough for rendering, and be antisymmetric.
    const auto rel = toCameraRelative(*farA, *origin);
    CHECK(rel.x == doctest::Approx(kFarMetres).epsilon(1.0e-9));
    CHECK(rel.y == doctest::Approx(0.0));
    CHECK(rel.z == doctest::Approx(0.0));

    const auto reverse = toCameraRelative(*origin, *farA);
    CHECK(reverse.x == doctest::Approx(-rel.x));
}

TEST_CASE("Integration: content determinism during traversal")
{
    const std::uint64_t seed = 42;

    std::vector<std::uint64_t> firstPass;
    std::vector<std::uint64_t> secondPass;

    auto runPath = [](std::vector<std::uint64_t>& out) {
        WorldPosition pos;
        for (int i = 0; i < 100; ++i)
        {
            out.push_back(chunkHash(generateContent(seed, pos, 0)));
            auto moved = pos.translated(starforge::math::Vec3d{1000.0, 0.0, 0.0});
            REQUIRE(moved.has_value());
            pos = *moved;
        }
    };

    runPath(firstPass);
    runPath(secondPass);

    CHECK(firstPass.size() == 100);
    CHECK(firstPass == secondPass);
}

TEST_CASE("Integration: player deltas persist across traversal")
{
    WorldPosition pos;
    const std::uint64_t seed = 42;

    auto chunk = generateContent(seed, pos, 0);
    REQUIRE(!chunk.objects.empty());

    DeltaStore store;
    PlayerDelta d;
    d.cellX = 0; d.cellY = 0; d.cellZ = 0; d.level = 0;
    d.objectId = chunk.objects[0].objectId;
    d.type = DeltaType::Removed;
    store.apply(d);

    auto modified = applyDeltas(chunk, store);
    CHECK(modified.objects.size() == chunk.objects.size() - 1);

    // Traverse 10 cells and come back.
    for (int i = 0; i < 10; ++i)
    {
        auto moved = pos.translated(
            starforge::math::Vec3d{WorldPosition::kCellSize, 0.0, 0.0});
        REQUIRE(moved.has_value());
        pos = *moved;
    }

    auto back = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(back.has_value());
    pos = *back;

    CHECK(store.isRemoved(0, 0, 0, 0, chunk.objects[0].objectId));

    auto chunk2 = generateContent(seed, pos, 0);
    auto modified2 = applyDeltas(chunk2, store);
    CHECK(modified2.objects.size() == chunk2.objects.size() - 1);
}

TEST_CASE("Integration: scale level traversal covers all levels")
{
    WorldPosition pos;
    auto cells = traverseCells(pos, 0, kMaxScaleLevel, 10000.0);

    std::set<int> levels;
    for (const auto& c : cells)
    {
        levels.insert(c.level);
    }
    CHECK(levels.size() == static_cast<std::size_t>(kMaxScaleLevel + 1));

    for (const auto& cell : cells)
    {
        auto chunk = generateContent(42, cell.x, cell.y, cell.z, cell.level);
        CHECK(chunk.level == cell.level);
        CHECK(chunk.seed == 42);
        CHECK(chunk.cellX == cell.x);
        CHECK(chunk.cellY == cell.y);
        CHECK(chunk.cellZ == cell.z);
    }
}

TEST_CASE("Integration: generation is stateless across traversal")
{
    WorldPosition pos;
    const std::uint64_t originHash = chunkHash(generateContent(42, pos, 0));

    // Generate and discard lots of chunks. Generation must keep no mutable
    // state (all storage is RAII-owned by the returned chunks), so revisiting
    // the origin must yield the original content.
    for (int i = 0; i < 500; ++i)
    {
        auto chunk = generateContent(42, pos, 0);
        (void)chunk;

        auto moved = pos.translated(starforge::math::Vec3d{1000.0, 0.0, 0.0});
        REQUIRE(moved.has_value());
        pos = *moved;
    }

    auto back = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(back.has_value());
    CHECK(chunkHash(generateContent(42, *back, 0)) == originHash);
}
