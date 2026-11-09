#include "content/content_generation.hpp"
#include "content/cell_traversal.hpp"
#include "content/hash.hpp"
#include "jobs/job_scheduler.hpp"

#include "spatial/world_position.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>

namespace starforge::content
{

namespace
{

// Uniform local coordinate in [0, cellSize).
// Multiplication can round up to exactly cellSize for very large cells
// (>= 2^53 m), which would place the object in the next cell.
double localCoordinate(DeterministicRng& rng, double cellSize) noexcept
{
    double v = rng.nextDouble() * cellSize;
    if (v >= cellSize)
    {
        v = std::nextafter(cellSize, 0.0);
    }
    return v;
}

// Generate a single content object from RNG state.
// local positions are in [0, cellSize) relative to the cell origin.
ContentObject generateObject(
    DeterministicRng& rng,
    std::uint64_t objectId,
    ContentType type,
    double cellSize)
{
    ContentObject obj;
    obj.objectId = objectId;
    obj.type = type;

    obj.localX = localCoordinate(rng, cellSize);
    obj.localY = localCoordinate(rng, cellSize);
    obj.localZ = localCoordinate(rng, cellSize);

    switch (type)
    {
    case ContentType::Star:
        // subtype: 0=red dwarf, 1=yellow, 2=blue giant, 3=white dwarf
        obj.subtype = static_cast<std::uint32_t>(rng.nextInt(0, 3));
        {
            std::uint32_t planetCount = static_cast<std::uint32_t>(rng.nextInt(0, 8));
            bool hasBelt = rng.nextFloat() < 0.4f;
            bool hasStation = rng.nextFloat() < 0.2f;
            obj.properties = planetCount | (hasBelt ? (1u << 4) : 0u) | (hasStation ? (1u << 5) : 0u);
            obj.scale = 10.0f + rng.nextFloat() * 90.0f; // stars are large
        }
        break;

    case ContentType::Planet:
        // subtype: 0=rocky, 1=gas giant, 2=ice, 3=ocean
        obj.subtype = static_cast<std::uint32_t>(rng.nextInt(0, 3));
        obj.scale = 0.5f + rng.nextFloat() * 3.0f;
        break;

    case ContentType::Asteroid:
        // subtype: mineral type (0=iron, 1=silicate, 2=carbon, 3=ice)
        obj.subtype = static_cast<std::uint32_t>(rng.nextInt(0, 3));
        {
            std::uint32_t sizeClass = static_cast<std::uint32_t>(rng.nextInt(0, 2)); // 0=small, 1=medium, 2=large
            obj.properties = (sizeClass << 4);
            obj.scale = 0.1f + rng.nextFloat() * 0.9f; // asteroids are small
        }
        // Vary orientation so toon light bands don't align per cluster.
        obj.orientation[0] = static_cast<std::uint8_t>(rng.nextInt(0, 255));
        obj.orientation[1] = static_cast<std::uint8_t>(rng.nextInt(0, 255));
        break;

    case ContentType::Station:
        // subtype: 0=trading post, 1=mining station, 2=research, 3=military
        obj.subtype = static_cast<std::uint32_t>(rng.nextInt(0, 3));
        {
            std::uint32_t faction = static_cast<std::uint32_t>(rng.nextInt(0, 3));
            std::uint32_t services = static_cast<std::uint32_t>(rng.nextInt(1, 15)); // bitmask of services
            std::uint32_t docking = static_cast<std::uint32_t>(rng.nextInt(1, 8));
            obj.properties = faction | (services << 4) | (docking << 8);
            obj.scale = 0.01f + rng.nextFloat() * 0.04f; // stations (small) are large
        }
        break;

    case ContentType::ResourceNode:
        // subtype: resource type (0=iron, 1=silicon, 2=carbon, 3=water)
        obj.subtype = static_cast<std::uint32_t>(rng.nextInt(0, 3));
        obj.scale = 0.2f + rng.nextFloat() * 0.8f;
        break;

    case ContentType::Nebula:
        obj.subtype = static_cast<std::uint32_t>(rng.nextInt(0, 2));
        obj.scale = 50.0f + rng.nextFloat() * 100.0f;
        break;
    }

    return obj;
}

// Determine how many objects of each type to generate based on level.
// Landmarks (stations, resource nodes, stars, planets) are generated
// before background clutter so a budget cap truncates clutter, never a
// point of interest.
void generateForLevel(DeterministicRng& rng, ContentChunk& chunk, int level, std::size_t objectCap)
{
    // Object counts decrease as level increases (coarser = fewer objects).
    int starCount = 0;
    int planetCount = 0;
    int asteroidCount = 0;
    int stationCount = 0;
    int resourceCount = 0;

    // Object IDs are allocated from one counter for the whole cell, so every
    // object in a chunk has a unique id (delta identity depends on this).
    std::uint64_t objectId = 0;

    const double cellSize = cellSizeAtLevel(level);

    bool hasNebula = false;
    if (level >= 4)
    {
        // Cluster / summary level.
        starCount = rng.nextInt(1, 3);
        hasNebula = rng.nextFloat() < 0.2f;
    }
    else if (level >= 3)
    {
        // Galaxy sector level.
        starCount = rng.nextInt(1, 3);
    }
    else if (level >= 1)
    {
        // System scale (Levels 1 and 2).
        starCount = rng.nextInt(0, 1);
        planetCount = rng.nextInt(0, 5);
        stationCount = (rng.nextFloat() < 0.3f) ? 1 : 0;
        asteroidCount = rng.nextInt(0, 2);
    }
    else
    {
        // Local scale (Level 0).
        asteroidCount = rng.nextInt(5, 50);
        resourceCount = rng.nextInt(0, 5);
        stationCount = (rng.nextFloat() < 0.1f) ? 1 : 0;
    }

    const std::size_t totalObjects = static_cast<std::size_t>(
        starCount + planetCount + asteroidCount + stationCount + resourceCount + (hasNebula ? 1 : 0));
    chunk.objects.reserve(std::min(totalObjects, objectCap));

    auto append = [&](int count, ContentType type) {
        for (int i = 0; i < count; ++i)
        {
            if (chunk.objects.size() >= objectCap)
            {
                return;
            }
            chunk.objects.push_back(generateObject(rng, objectId++, type, cellSize));
        }
    };

    append(starCount, ContentType::Star);
    append(planetCount, ContentType::Planet);
    // Landmarks before clutter.
    append(stationCount, ContentType::Station);
    append(resourceCount, ContentType::ResourceNode);
    if (hasNebula)
    {
        append(1, ContentType::Nebula);
    }
    // Background clutter last.
    append(asteroidCount, ContentType::Asteroid);
}

} // namespace

ContentChunk generateContent(
    std::uint64_t seed,
    std::int64_t cellX, std::int64_t cellY, std::int64_t cellZ,
    int level)
{
    if (level < 0) level = 0;
    if (level > kMaxScaleLevel) level = kMaxScaleLevel;

    ContentChunk chunk;
    chunk.cellX = cellX;
    chunk.cellY = cellY;
    chunk.cellZ = cellZ;
    chunk.level = level;
    chunk.seed = seed;

    std::uint64_t s = contentSeed(seed, cellX, cellY, cellZ, level);
    DeterministicRng rng(s);

    generateForLevel(rng, chunk, level, std::numeric_limits<std::size_t>::max());

    return chunk;
}

ContentChunk generateContentBudgeted(std::uint64_t seed,
                                     std::int64_t cellX, std::int64_t cellY, std::int64_t cellZ,
                                     int level,
                                     const GenerationBudget& budget)
{
    if (level < 0) level = 0;
    if (level > kMaxScaleLevel) level = kMaxScaleLevel;

    ContentChunk chunk;
    chunk.cellX = cellX;
    chunk.cellY = cellY;
    chunk.cellZ = cellZ;
    chunk.level = level;
    chunk.seed = seed;

    std::uint64_t s = contentSeed(seed, cellX, cellY, cellZ, level);
    DeterministicRng rng(s);
    generateForLevel(rng, chunk, level, objectCapForLevel(budget, level));
    return chunk;
}

ContentChunk generateContentBudgeted(std::uint64_t seed,
                                     const starforge::spatial::WorldPosition& pos,
                                     int level,
                                     const GenerationBudget& budget)
{
    const auto cell = worldToCell(pos, level);
    return generateContentBudgeted(seed, cell.x, cell.y, cell.z, cell.level, budget);
}

TimedContentChunk generateContentTimed(std::uint64_t seed,
                                       std::int64_t cellX, std::int64_t cellY, std::int64_t cellZ,
                                       int level,
                                       const GenerationBudget& budget)
{
    const auto start = std::chrono::steady_clock::now();
    ContentChunk chunk = generateContentBudgeted(seed, cellX, cellY, cellZ, level, budget);
    const auto end = std::chrono::steady_clock::now();

    TimedContentChunk timed;
    timed.chunk = std::move(chunk);
    timed.generationMillis =
        std::chrono::duration<double, std::milli>(end - start).count();
    timed.overGenerationBudget =
        timed.generationMillis > static_cast<double>(budget.maxGenerationMicrosPerChunk) / 1000.0;
    return timed;
}

std::size_t objectCapForLevel(const GenerationBudget& budget, int level) noexcept
{
    if (level < 0)
    {
        level = 0;
    }
    if (level > kMaxScaleLevel)
    {
        level = kMaxScaleLevel;
    }
    return budget.objectCaps[static_cast<std::size_t>(level)];
}

std::size_t chunkStagingBytes(const ContentChunk& chunk) noexcept
{
    return chunk.objects.size() * sizeof(ContentObject);
}

std::uint32_t chunkDrawCalls(const ContentChunk& chunk) noexcept
{
    // Instanced draw cost: one draw call per distinct (type, subtype)
    // archetype, not one per asteroid. With the generator's fixed
    // subtype vocab this is at most 24, so a full chunk uploads inside
    // the 32-draw frame budget.
    std::array<std::uint64_t, 64> seen{};
    std::uint32_t archetypes = 0;
    for (const auto& object : chunk.objects)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(object.type) << 32U) | object.subtype;
        bool known = false;
        for (std::uint32_t i = 0; i < archetypes; ++i)
        {
            if (seen[i] == key)
            {
                known = true;
                break;
            }
        }
        if (!known && archetypes < seen.size())
        {
            seen[archetypes++] = key;
        }
    }
    return archetypes;
}

ContentChunk generateContent(
    std::uint64_t seed,
    const starforge::spatial::WorldPosition& pos,
    int level)
{
    // worldToCell owns level clamping and the coarse-cell hierarchy semantics.
    auto cell = worldToCell(pos, level);
    return generateContent(seed, cell.x, cell.y, cell.z, cell.level);
}

std::vector<ContentChunk> generateContentParallel(std::uint64_t seed,
                                                  std::span<const CellCoord> cells,
                                                  std::size_t maxInFlightBatches,
                                                  const GenerationBudget& budget)
{
    if (cells.empty())
    {
        return {};
    }

    std::vector<ContentChunk> results(cells.size());

    constexpr std::size_t kBatchSize = 16;
    const std::size_t batchCount = (cells.size() + kBatchSize - 1) / kBatchSize;
    if (maxInFlightBatches == 0)
    {
        maxInFlightBatches = 1;
    }

    struct BatchContext
    {
        std::uint64_t seed;
        std::span<const CellCoord> cells;
        ContentChunk* outputBase;
        const GenerationBudget* budget;
    };

    std::vector<BatchContext> batches(batchCount);
    for (std::size_t b = 0; b < batchCount; ++b)
    {
        const std::size_t startIdx = b * kBatchSize;
        const std::size_t count = std::min(kBatchSize, cells.size() - startIdx);
        batches[b] = BatchContext{
            .seed = seed,
            .cells = cells.subspan(startIdx, count),
            .outputBase = &results[startIdx],
            .budget = &budget,
        };
    }

    auto batchFn = [](void* p) {
        auto* ctx = static_cast<BatchContext*>(p);
        for (std::size_t i = 0; i < ctx->cells.size(); ++i)
        {
            const auto& coord = ctx->cells[i];
            ctx->outputBase[i] =
                generateContentBudgeted(ctx->seed, coord.x, coord.y, coord.z, coord.level, *ctx->budget);
        }
    };

    // Sliding window (not waves): submit replacements as batches finish
    // instead of barrier-waiting the whole wave, so one slow batch
    // stalls no worker. No more than maxInFlightBatches compete at a time.
    std::deque<starforge::jobs::TaskHandle> inFlight;
    std::size_t nextBatch = 0;
    while (nextBatch < batchCount || !inFlight.empty())
    {
        while (nextBatch < batchCount && inFlight.size() < maxInFlightBatches)
        {
            inFlight.push_back(starforge::jobs::submit(batchFn, &batches[nextBatch]));
            ++nextBatch;
        }
        starforge::jobs::globalScheduler().wait(inFlight.front());
        inFlight.pop_front();
    }

    return results;
}

} // namespace starforge::content
