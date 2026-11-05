#pragma once

#include "content/cell_traversal.hpp"
#include "spatial/world_position.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace starforge::content
{

// Type of generated content object.
enum class ContentType : std::uint8_t
{
    Star,
    Planet,
    Asteroid,
    Station,
    ResourceNode,
    Nebula,
};

// A single generated content object within a cell.
enum class StarSubtype : std::uint32_t { RedDwarf = 0, Yellow, BlueGiant, WhiteDwarf };
enum class PlanetSubtype : std::uint32_t { Rocky = 0, GasGiant, Ice, Ocean };
enum class StationSubtype : std::uint32_t { TradingPost = 0, Mining, Research, Military };
enum class AsteroidSubtype : std::uint32_t { Iron = 0, Silicate, Carbon, Ice };
enum class ResourceSubtype : std::uint32_t { Mineral = 0, Gas, Crystal, Exotic };

struct ContentObject
{
    // Position within the cell (metres, relative to the cell origin).
    // Always in [0, cellSizeAtLevel(chunk.level)).
    double localX = 0.0;
    double localY = 0.0;
    double localZ = 0.0;

    std::uint64_t objectId = 0;  // unique within cell

    float scale = 1.0f;
    std::uint32_t subtype = 0;
    std::uint32_t properties = 0;

    ContentType type = ContentType::Asteroid;

    std::uint8_t padding[3] = {0, 0, 0};
};

// Packed property accessors (star).
inline std::uint32_t starPlanetCount(const ContentObject& o) noexcept { return o.properties & 0x0F; }
inline bool starHasAsteroidBelt(const ContentObject& o) noexcept { return (o.properties >> 4) & 1; }
inline bool starHasStation(const ContentObject& o) noexcept { return (o.properties >> 5) & 1; }

// Packed property accessors (asteroid). Mineral lives in `subtype`.
inline std::uint8_t asteroidMineral(const ContentObject& o) noexcept { return static_cast<std::uint8_t>(o.subtype); }
inline std::uint8_t asteroidSizeClass(const ContentObject& o) noexcept { return static_cast<std::uint8_t>((o.properties >> 4) & 0x03); }

// Packed property accessors (station).
inline std::uint8_t stationFaction(const ContentObject& o) noexcept { return static_cast<std::uint8_t>(o.properties & 0x0F); }
inline std::uint8_t stationServices(const ContentObject& o) noexcept { return static_cast<std::uint8_t>((o.properties >> 4) & 0x0F); }
inline std::uint8_t stationDockingCapacity(const ContentObject& o) noexcept { return static_cast<std::uint8_t>((o.properties >> 8) & 0x0F); }

// A chunk of generated content for a single cell at a given scale level.
struct ContentChunk
{
    std::int64_t cellX = 0;
    std::int64_t cellY = 0;
    std::int64_t cellZ = 0;
    int level = 0;              // scale level (0 = finest, higher = coarser)
    std::uint64_t seed = 0;     // world seed
    std::vector<ContentObject> objects;
};

// Generate content for a cell at a given scale level.
// Pure function: same (seed, cell, level) → identical output.
// Seed derivation and object identity use only integer arithmetic. Object
// transforms use IEEE-754 floating-point; the content library is compiled
// with -ffp-contract=off so results are not altered by FMA contraction.
// Levels are clamped to [0, kMaxScaleLevel].
[[nodiscard]] ContentChunk generateContent(
    std::uint64_t seed,
    std::int64_t cellX, std::int64_t cellY, std::int64_t cellZ,
    int level);

// Generate content from a WorldPosition's coarse cell.
// Delegates cell resolution to worldToCell() so scale levels are consistent
// across the content and spatial layers.
[[nodiscard]] ContentChunk generateContent(
    std::uint64_t seed,
    const starforge::spatial::WorldPosition& pos,
    int level);

// Generate content for multiple cells in parallel using the job system.
// Results are returned in the same order as the input cells.
// Pure function: same inputs → identical output regardless of thread count.
[[nodiscard]] std::vector<ContentChunk> generateContentParallel(
    std::uint64_t seed,
    std::span<const CellCoord> cells);

// Typed accessors for ContentObject subtypes.
enum class NebulaSubtype : std::uint32_t { Emission = 0, Dark, Reflection };

inline StarSubtype starSubtype(const ContentObject& o) noexcept { return static_cast<StarSubtype>(o.subtype); }
inline PlanetSubtype planetSubtype(const ContentObject& o) noexcept { return static_cast<PlanetSubtype>(o.subtype); }
inline StationSubtype stationSubtype(const ContentObject& o) noexcept { return static_cast<StationSubtype>(o.subtype); }
inline AsteroidSubtype asteroidSubtype(const ContentObject& o) noexcept { return static_cast<AsteroidSubtype>(o.subtype); }
inline ResourceSubtype resourceSubtype(const ContentObject& o) noexcept { return static_cast<ResourceSubtype>(o.subtype); }
inline NebulaSubtype nebulaSubtype(const ContentObject& o) noexcept { return static_cast<NebulaSubtype>(o.subtype); }

} // namespace starforge::content