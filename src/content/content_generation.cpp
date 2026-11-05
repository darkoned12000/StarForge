#include "content/content_generation.hpp"
#include "content/cell_traversal.hpp"
#include "content/hash.hpp"

#include "spatial/world_position.hpp"

#include <cmath>

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
        break;

    case ContentType::Station:
        // subtype: 0=trading post, 1=mining station, 2=research, 3=military
        obj.subtype = static_cast<std::uint32_t>(rng.nextInt(0, 3));
        {
            std::uint32_t faction = static_cast<std::uint32_t>(rng.nextInt(0, 3));
            std::uint32_t services = static_cast<std::uint32_t>(rng.nextInt(1, 15)); // bitmask of services
            std::uint32_t docking = static_cast<std::uint32_t>(rng.nextInt(1, 8));
            obj.properties = faction | (services << 4) | (docking << 8);
            obj.scale = 1.0f + rng.nextFloat() * 4.0f; // stations are large
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
void generateForLevel(DeterministicRng& rng, ContentChunk& chunk, int level)
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
        // Galaxy level: stars + occasional nebulae.
        starCount = rng.nextInt(1, 3);
        // Roll nebula probability upfront (deterministic count).
        hasNebula = rng.nextFloat() < 0.2f;
    }
    else if (level >= 2)
    {
        // System level: stars + planets + stations.
        starCount = rng.nextInt(0, 1);
        planetCount = rng.nextInt(0, 5);
        stationCount = (rng.nextFloat() < 0.3f) ? 1 : 0;
        asteroidCount = rng.nextInt(0, 2);
    }
    else
    {
        // Local level: asteroids, resources, stations.
        asteroidCount = rng.nextInt(5, 50);
        resourceCount = rng.nextInt(0, 5);
        stationCount = (rng.nextFloat() < 0.1f) ? 1 : 0;
    }

    for (int i = 0; i < starCount; ++i)
    {
        chunk.objects.push_back(generateObject(rng, objectId++, ContentType::Star, cellSize));
    }
    for (int i = 0; i < planetCount; ++i)
    {
        chunk.objects.push_back(generateObject(rng, objectId++, ContentType::Planet, cellSize));
    }
    for (int i = 0; i < asteroidCount; ++i)
    {
        chunk.objects.push_back(generateObject(rng, objectId++, ContentType::Asteroid, cellSize));
    }
    for (int i = 0; i < stationCount; ++i)
    {
        chunk.objects.push_back(generateObject(rng, objectId++, ContentType::Station, cellSize));
    }
    for (int i = 0; i < resourceCount; ++i)
    {
        chunk.objects.push_back(generateObject(rng, objectId++, ContentType::ResourceNode, cellSize));
    }

    if (hasNebula)
    {
        chunk.objects.push_back(generateObject(rng, objectId++, ContentType::Nebula, cellSize));
    }
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

    generateForLevel(rng, chunk, level);

    return chunk;
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

} // namespace starforge::content
