#include <doctest/doctest.h>

#include "content/cell_traversal.hpp"
#include "content/chunk_cache.hpp"
#include "content/upload_budget.hpp"
#include "ecs/world.hpp"

#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

using namespace starforge::content;
using namespace starforge::ecs;
using namespace starforge::spatial;

namespace
{

using CellKey = std::tuple<std::int64_t, std::int64_t, std::int64_t, int>;

[[nodiscard]] CellKey cellKey(const CellCoord& cell) noexcept
{
    return {cell.x, cell.y, cell.z, cell.level};
}

} // namespace

TEST_CASE("Flythrough: generated field streams through ECS for 2 simulated minutes")
{
    constexpr std::uint64_t kSeed = 42;
    constexpr double kStepSeconds = 1.0 / 60.0;
    constexpr int kFrames = 7200; // 2 minutes at 60 fps, 1000 m/s.

    RawGenerationStore cache(RawGenerationStore::Config{.maxEntries = 128, .maxBytes = 8 * 1024 * 1024});
    StreamingTraversal stream(5000.0);
    UploadBudget uploads;
    GenerationBudget budget = uploads.budget();

    Ecs world;
    std::map<CellKey, std::vector<flecs::entity>> mounted;
    std::size_t totalInstantiated = 0;
    std::size_t peakEntities = 0;
    std::size_t peakUploadBytes = 0;
    std::uint32_t peakUploadDrawCalls = 0;

    auto camera = WorldPosition::fromMeters(960000.0, WorldPosition::kCellSize * 0.5, WorldPosition::kCellSize * 0.5);
    REQUIRE(camera.has_value());

    for (int frame = 0; frame < kFrames; ++frame)
    {
        auto moved = camera->translated({1000.0 * kStepSeconds, 0.0, 0.0});
        REQUIRE(moved.has_value());
        camera = *moved;

        const auto update = stream.advance(*camera, 0, 0, 5000.0);
        for (const auto& cell : update.loaded)
        {
            const auto chunk = cache.get(kSeed, cell, budget);
            REQUIRE(chunk != nullptr);
            const auto decision = uploads.reserve(chunkStagingBytes(*chunk), chunkDrawCalls(*chunk));
            REQUIRE(decision.accepted);

            const auto cellOrigin = WorldPosition::fromCellAndOffset(cell.x, cell.y, cell.z, 0.0, 0.0, 0.0);
            REQUIRE(cellOrigin.has_value());
            for (const auto& object : chunk->objects)
            {
                const auto position = cellOrigin->translated({object.localX, object.localY, object.localZ});
                REQUIRE(position.has_value());
                const auto entity = world.createAsteroid(*position, {});
                auto& transform = entity.get_mut<Transform>();
                transform.scale = object.scale;
                // Deterministic per-object orientation from the packed
                // bytes: neighbouring asteroids must not share a light
                // cut-angle in the toon pipeline.
#include <glm/gtc/quaternion.hpp>
                const glm::quat yaw =
                    glm::angleAxis(static_cast<float>(asteroidYawRadians(object)), glm::vec3(0.0f, 1.0f, 0.0f));
                const glm::quat pitch =
                    glm::angleAxis(static_cast<float>(asteroidPitchRadians(object)), glm::vec3(1.0f, 0.0f, 0.0f));
                const glm::quat orientation = yaw * pitch;
                transform.orientation = {orientation.x, orientation.y, orientation.z, orientation.w};
                entity.modified<Transform>();
                mounted[cellKey(cell)].push_back(entity);
                ++totalInstantiated;
            }
        }
        for (const auto& cell : update.unloaded)
        {
            const auto group = mounted.find(cellKey(cell));
            if (group == mounted.end())
            {
                continue;
            }
            for (const auto entity : group->second)
            {
                entity.destruct();
            }
            mounted.erase(group);
        }
        peakUploadBytes = std::max(peakUploadBytes, uploads.bytesUsed());
        peakUploadDrawCalls = std::max(peakUploadDrawCalls, uploads.drawCallsUsed());
        uploads.resetFrame();
        peakEntities = std::max(peakEntities, world.entityCount());
        if (frame % 60 == 0)
        {
            world.step(1.0f);
        }
    }

    // Reverse the flight: every re-entered cell must hit the raw
    // generation cache rather than generating new bytes.
    for (int frame = 0; frame < kFrames; ++frame)
    {
        auto moved = camera->translated({-1000.0 * kStepSeconds, 0.0, 0.0});
        REQUIRE(moved.has_value());
        camera = *moved;

        const auto update = stream.advance(*camera, 0, 0, 5000.0);
        for (const auto& cell : update.loaded)
        {
            const auto chunk = cache.get(kSeed, cell, budget);
            REQUIRE(chunk != nullptr);
            const auto decision = uploads.reserve(chunkStagingBytes(*chunk), chunkDrawCalls(*chunk));
            REQUIRE(decision.accepted);
            peakUploadBytes = std::max(peakUploadBytes, uploads.bytesUsed());
            peakUploadDrawCalls = std::max(peakUploadDrawCalls, uploads.drawCallsUsed());

            const auto cellOrigin = WorldPosition::fromCellAndOffset(cell.x, cell.y, cell.z, 0.0, 0.0, 0.0);
            REQUIRE(cellOrigin.has_value());
            for (const auto& object : chunk->objects)
            {
                const auto position = cellOrigin->translated({object.localX, object.localY, object.localZ});
                REQUIRE(position.has_value());
                const auto entity = world.createAsteroid(*position, {});
                auto& transform = entity.get_mut<Transform>();
                transform.scale = object.scale;
                entity.modified<Transform>();
                mounted[cellKey(cell)].push_back(entity);
                ++totalInstantiated;
            }
        }
        for (const auto& cell : update.unloaded)
        {
            const auto group = mounted.find(cellKey(cell));
            if (group == mounted.end())
            {
                continue;
            }
            for (const auto entity : group->second)
            {
                entity.destruct();
            }
            mounted.erase(group);
        }
        uploads.resetFrame();
    }

    // The flight crosses one level-0 boundary: it generates each
    // of the two cells once, and both stay resident through the cache.
    CHECK(cache.stats().misses == 2);
    CHECK(cache.stats().hits > 0);
    CHECK(cache.entryCount() == 2);
    CHECK(peakEntities <= budget.objectCaps[0] * 2);
    CHECK(world.entityCount() <= budget.objectCaps[0] * 2);
    CHECK(totalInstantiated > 0);
    CHECK(peakEntities <= budget.objectCaps[0] * 2);

    // The flythrough never touched more than the frame upload budget.
    CHECK(peakUploadBytes > 0);
    CHECK(peakUploadBytes <= budget.maxUploadBytesPerFrame);
    CHECK(peakUploadDrawCalls <= budget.maxUploadDrawCallsPerFrame);
}
