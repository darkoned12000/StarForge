#include <doctest/doctest.h>

#include "content/cell_traversal.hpp"
#include "content/chunk_cache.hpp"
#include "ecs/content_instantiation.hpp"
#include "ecs/world.hpp"

#include <cmath>

using namespace starforge::content;
using namespace starforge::ecs;
using namespace starforge::spatial;

TEST_CASE("Interstellar flight: far anchor generates deterministic, precise content")
{
    constexpr std::uint64_t kSeed = 42;
    constexpr double kStepSeconds = 1.0 / 60.0;
    constexpr int kFrames = 7200; // 2 minutes at 1,000 m/s.

    // Anchor at level-0 cell 1,000,000: its base is
    // 1,000,000 * 2^20 = 1,048,576,000,000 m (> 1e12 m), and the
    // 960,000 m offset puts the boundary 88,576 m ahead, so the
    // 120,000 m flight genuinely crosses into the next cell.
    const auto anchor = WorldPosition::fromCellAndOffset(
        1000000, 0, 0, 960000.0, WorldPosition::kCellSize * 0.5, WorldPosition::kCellSize * 0.5);
    REQUIRE(anchor.has_value());
    auto camera = *anchor;
    CHECK(camera.cellX == 1000000);

    RawGenerationStore cache(RawGenerationStore::Config{.maxEntries = 128, .maxBytes = 8 * 1024 * 1024});
    StreamingTraversal stream(5000.0);
    Ecs world;
    std::vector<MountedChunk> mounted;

    for (int frame = 0; frame < kFrames; ++frame)
    {
        auto moved = camera.translated({1000.0 * kStepSeconds, 0.0, 0.0});
        REQUIRE(moved.has_value());
        camera = *moved;

        const auto update = stream.advance(camera, 0, 0, 5000.0);
        for (const auto& cell : update.loaded)
        {
            const auto chunk = cache.get(kSeed, cell);
            REQUIRE(chunk != nullptr);
            REQUIRE(!chunk->objects.empty());
            mounted.push_back(instantiateChunk(world, *chunk));
        }
        for (const auto& cell : update.unloaded)
        {
            for (auto& mount : mounted)
            {
                if (mount.cell.x == cell.x && mount.cell.y == cell.y && mount.cell.z == cell.z &&
                    mount.cell.level == cell.level)
                {
                    unmountChunk(mount);
                }
            }
        }
        mounted.erase(std::remove_if(mounted.begin(), mounted.end(),
                                     [](const MountedChunk& mount) { return mount.entities.empty(); }),
                      mounted.end());
    }

    // Precision: the camera really is more than 1e12 m from the origin.
    const double distanceFromOrigin = std::abs(camera.toMeters().x);
    CHECK(distanceFromOrigin > 1.0e12);
    CHECK(camera.cellX == 1000001);

    // The boundary crossing mounted both cells exactly once each.
    CHECK(cache.stats().misses == 2);
    CHECK(cache.entryCount() == 2);
    // Repeat lookups for the origin cell are cache hits, not regeneration.
    (void)cache.get(kSeed, stream.cells().front());
    CHECK(cache.stats().hits > 0);

    // The final cells are deterministic: regenerating them directly
    // matches what the stream produced.
    for (const auto& cell : stream.cells())
    {
        const auto fresh = generateContent(kSeed, cell.x, cell.y, cell.z, cell.level);
        const auto cached = cache.get(kSeed, cell);
        REQUIRE(cached != nullptr);
        CHECK(cached->objects.size() == fresh.objects.size());
        for (std::size_t i = 0; i < fresh.objects.size(); ++i)
        {
            CHECK(cached->objects[i].objectId == fresh.objects[i].objectId);
            CHECK(cached->objects[i].localX == fresh.objects[i].localX);
        }
    }

    // Everything the stream loaded mounted without exploding the pool.
    CHECK(world.entityCount() <= 64 * 2);
    CHECK(world.entityCount() > 0);
}
