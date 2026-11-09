#include <doctest/doctest.h>

#include "content/content_generation.hpp"
#include "content/upload_budget.hpp"
#include "jobs/job_scheduler.hpp"

#include <vector>

using namespace starforge::content;

namespace
{

[[nodiscard]] bool chunksEqual(const ContentChunk& a, const ContentChunk& b)
{
    if (a.cellX != b.cellX || a.cellY != b.cellY || a.cellZ != b.cellZ || a.level != b.level || a.seed != b.seed ||
        a.objects.size() != b.objects.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.objects.size(); ++i)
    {
        const auto& x = a.objects[i];
        const auto& y = b.objects[i];
        if (x.objectId != y.objectId || x.type != y.type || x.localX != y.localX || x.localY != y.localY ||
            x.localZ != y.localZ || x.scale != y.scale)
        {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Content budgets: object cap truncates deterministically")
{
    const auto uncapped = generateContent(42, 0, 0, 0, 0);
    GenerationBudget budget;
    budget.objectCaps[0] = 10;

    const auto capped = generateContentBudgeted(42, 0, 0, 0, 0, budget);
    REQUIRE(capped.objects.size() <= 10);
    REQUIRE(capped.objects.size() == 10);
    for (std::size_t i = 0; i < capped.objects.size(); ++i)
    {
        CHECK(capped.objects[i].objectId == uncapped.objects[i].objectId);
        CHECK(capped.objects[i].localX == uncapped.objects[i].localX);
    }

    // A generous cap reproduces the uncapped chunk exactly.
    GenerationBudget generous;
    generous.objectCaps[0] = 1024;
    CHECK(chunksEqual(generateContentBudgeted(42, 0, 0, 0, 0, generous), uncapped));
}

TEST_CASE("Content budgets: staging bytes and draw calls derive from object count")
{
    const auto chunk = generateContent(42, 0, 0, 0, 0);
    CHECK(chunkStagingBytes(chunk) == chunk.objects.size() * sizeof(ContentObject));
    // Instanced: one draw call per distinct archetype, not per object.
    CHECK(chunkDrawCalls(chunk) < chunk.objects.size());
    CHECK(chunkDrawCalls(chunk) <= 32);
}

TEST_CASE("Content budgets: timed generation records wall-clock time")
{
    const auto timed = generateContentTimed(42, 0, 0, 0, 0, {});
    CHECK(timed.chunk.objects.size() == generateContent(42, 0, 0, 0, 0).objects.size());
    CHECK(timed.generationMillis >= 0.0);
    // The reference generator is far below the 2 ms per-chunk cap on the
    // target hardware; the flag would trip only on pathological input.
    CHECK(!timed.overGenerationBudget);
}

TEST_CASE("Content budgets: upload budget is all-or-nothing per frame")
{
    GenerationBudget budget;
    budget.maxUploadBytesPerFrame = 1000;
    budget.maxUploadDrawCallsPerFrame = 4;
    UploadBudget tracker(budget);

    const auto first = tracker.reserve(400, 1);
    REQUIRE(first.accepted);
    CHECK(first.bytesUsed == 400);
    CHECK(first.drawCallsUsed == 1);

    const auto filled = tracker.reserve(600, 3);
    REQUIRE(filled.accepted);
    CHECK(filled.bytesUsed == 1000);
    CHECK(filled.drawCallsUsed == 4);

    // Neither a byte nor a draw call fits; the request is rejected
    // without consuming budget.
    const auto rejected = tracker.reserve(1, 0);
    CHECK(!rejected.accepted);
    CHECK(rejected.bytesUsed == 1000);
    CHECK(tracker.bytesUsed() == 1000);
    CHECK(tracker.drawCallsUsed() == 4);

    tracker.resetFrame();
    CHECK(tracker.bytesUsed() == 0);
    CHECK(tracker.reserve(1, 1).accepted);
}

TEST_CASE("Content budgets: chunk estimates fit the frame budget")
{
    GenerationBudget budget;
    UploadBudget tracker(budget);

    const auto chunk = generateContentBudgeted(42, 0, 0, 0, 0, budget);
    const auto decision = tracker.reserve(chunkStagingBytes(chunk), chunkDrawCalls(chunk));
    REQUIRE(decision.accepted);
    CHECK(decision.bytesUsed > 0);
    CHECK(decision.drawCallsUsed > 1);
}

TEST_CASE("Parallel generation: wave cap stays bit-identical to sequential")
{
    const std::uint64_t seed = 42;
    using namespace starforge::content;
    const auto camera = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camera.has_value());

    std::vector<CellCoord> cells;
    for (int x = 0; x < 64; ++x)
    {
        cells.push_back({x, 0, 0, 0});
    }
    for (int level = 1; level <= 2; ++level)
    {
        cells.push_back({0, 0, 0, level});
    }

    starforge::jobs::initGlobalScheduler(starforge::jobs::SchedulerConfig{.threadCount = 4});
    const auto parallel = generateContentParallel(seed, cells, 4);
    starforge::jobs::shutdownGlobalScheduler();

    REQUIRE(parallel.size() == cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        const auto sequential = generateContent(seed, cells[i].x, cells[i].y, cells[i].z, cells[i].level);
        CHECK(chunksEqual(parallel[i], sequential));
    }
}

TEST_CASE("Parallel generation: budget is honoured on the job path")
{
    const std::uint64_t seed = 42;
    using namespace starforge::content;

    std::vector<CellCoord> cells;
    for (int x = 0; x < 32; ++x)
    {
        cells.push_back({x, 0, 0, 0});
    }

    GenerationBudget budget;
    budget.objectCaps[0] = 5;

    starforge::jobs::initGlobalScheduler(starforge::jobs::SchedulerConfig{.threadCount = 2});
    const auto parallel = generateContentParallel(seed, cells, 4, budget);
    starforge::jobs::shutdownGlobalScheduler();

    REQUIRE(parallel.size() == cells.size());
    for (const auto& chunk : parallel)
    {
        CHECK(chunk.objects.size() <= 5);
    }
}

TEST_CASE("Content budgets: truncation prefers landmarks over clutter")
{
    // Find a cell whose uncapped generation would exceed a small cap and
    // still contains landmarks.
    for (std::uint64_t seed = 0; seed < 100; ++seed)
    {
        const auto uncapped = generateContent(seed, 0, 0, 0, 0);
        bool hasLandmark = false;
        for (const auto& object : uncapped.objects)
        {
            if (object.type == ContentType::Station || object.type == ContentType::ResourceNode)
            {
                hasLandmark = true;
            }
        }
        if (!hasLandmark)
        {
            continue;
        }
        GenerationBudget budget;
        budget.objectCaps[0] = 3;
        const auto capped = generateContentBudgeted(seed, 0, 0, 0, 0, budget);
        bool cappedHasLandmark = false;
        for (const auto& object : capped.objects)
        {
            if (object.type == ContentType::Station || object.type == ContentType::ResourceNode)
            {
                cappedHasLandmark = true;
            }
        }
        CHECK(cappedHasLandmark);
        return;
    }
    FAIL("expected a seeded cell with a landmark within 100 seeds");
}

TEST_CASE("Content budgets: asteroid orientation varies within a cluster")
{
    const auto chunk = generateContent(42, 0, 0, 0, 0);
    bool varied = false;
    std::uint8_t firstYaw = 0;
    for (const auto& object : chunk.objects)
    {
        if (object.type == ContentType::Asteroid)
        {
            if (asteroidYawQuantized(object) != firstYaw)
            {
                varied = true;
            }
            firstYaw = asteroidYawQuantized(object);
            const double yaw = asteroidYawRadians(object);
            CHECK(yaw >= 0.0);
            CHECK(yaw < 2.0 * 3.141592653589793);
        }
    }
    CHECK(varied);
}
