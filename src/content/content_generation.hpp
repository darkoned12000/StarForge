#pragma once

#include "content/cell_traversal.hpp"
#include "spatial/world_position.hpp"

#include <array>
#include <cstdint>
#include <numbers>
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

    // Quantized orientation bytes packed into the former padding:
    // [0] yaw (0..255 -> 0..2pi), [1] pitch, [2] reserved.
    std::uint8_t orientation[3] = {0, 0, 0};
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

// Quantized orientation accessors: varying light bands across
// instances of the same archetype, without inflating the struct.
inline std::uint8_t asteroidYawQuantized(const ContentObject& o) noexcept { return o.orientation[0]; }
inline std::uint8_t asteroidPitchQuantized(const ContentObject& o) noexcept { return o.orientation[1]; }
[[nodiscard]] inline double asteroidYawRadians(const ContentObject& o) noexcept
{
    return static_cast<double>(asteroidYawQuantized(o)) * (2.0 * std::numbers::pi_v<double> / 256.0);
}
[[nodiscard]] inline double asteroidPitchRadians(const ContentObject& o) noexcept
{
    return static_cast<double>(asteroidPitchQuantized(o)) * (2.0 * std::numbers::pi_v<double> / 256.0);
}

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

// Per-cell CPU/GPU budget inputs. The defaults keep generation below the
// frame budget without constraining the current M6 content profile.
struct GenerationBudget
{
    std::array<std::size_t, kMaxScaleLevel + 1> objectCaps{64, 32, 16, 16, 16};
    std::uint64_t maxGenerationMicrosPerChunk = 2000;
    std::size_t maxUploadBytesPerFrame = 2 * 1024 * 1024;
    std::uint32_t maxUploadDrawCallsPerFrame = 32;
};

// A generated chunk plus the wall-clock time spent generating it.
struct TimedContentChunk
{
    ContentChunk chunk;
    double generationMillis = 0.0;
    bool overGenerationBudget = false;
};

// Generate content for a cell at a given scale level, bounded by the
// per-level object cap. Pure and deterministic: a capped chunk is the
// unbounded chunk truncated to its first `objectCaps[level]` objects.
[[nodiscard]] ContentChunk generateContentBudgeted(std::uint64_t seed,
                                                   std::int64_t cellX, std::int64_t cellY, std::int64_t cellZ,
                                                   int level,
                                                   const GenerationBudget& budget = {});

[[nodiscard]] ContentChunk generateContentBudgeted(std::uint64_t seed,
                                                   const starforge::spatial::WorldPosition& pos,
                                                   int level,
                                                   const GenerationBudget& budget = {});

// Generates one cell while recording its CPU wall-clock time. `budget`
// caps the object count; the returned `overGenerationBudget` flag reports
// whether the measured time exceeded the per-chunk cap.
[[nodiscard]] TimedContentChunk generateContentTimed(std::uint64_t seed,
                                                     std::int64_t cellX, std::int64_t cellY, std::int64_t cellZ,
                                                     int level,
                                                     const GenerationBudget& budget = {});

[[nodiscard]] std::size_t objectCapForLevel(const GenerationBudget& budget, int level) noexcept;
[[nodiscard]] std::size_t chunkStagingBytes(const ContentChunk& chunk) noexcept;
[[nodiscard]] std::uint32_t chunkDrawCalls(const ContentChunk& chunk) noexcept;

// Generate content for a cell at a given scale level.
// Pure function: same (seed, cell, level) → identical output.
// Seed derivation and object identity use only integer arithmetic. Object
// transforms use IEEE-754 floating-point; the content library is compiled
// with -ffp-contract=off so results are not altered by FMA contraction.
// Levels are clamped to [0, kMaxScaleLevel].
[[nodiscard]] ContentChunk generateContent(std::uint64_t seed,
                                           std::int64_t cellX, std::int64_t cellY, std::int64_t cellZ,
                                           int level);

// Generate content from a WorldPosition's coarse cell.
// Delegates cell resolution to worldToCell() so scale levels are consistent
// across the content and spatial layers.
[[nodiscard]] ContentChunk generateContent(std::uint64_t seed,
                                           const starforge::spatial::WorldPosition& pos,
                                           int level);

// Generate content for multiple cells in parallel using the job system.
// Results are returned in the same order as the input cells.
// Pure function: same inputs → identical output regardless of thread count.
// At most `maxInFlightBatches` batches are submitted concurrently, so a
// large traversal neighborhood does not flood the job queue. When given a
// budget, batches are generated through the budgeted path so per-level
// object caps apply to parallel work as well.
[[nodiscard]] std::vector<ContentChunk> generateContentParallel(std::uint64_t seed,
                                                                std::span<const CellCoord> cells,
                                                                std::size_t maxInFlightBatches = 4,
                                                                const GenerationBudget& budget = {});

// Typed accessors for ContentObject subtypes.
enum class NebulaSubtype : std::uint32_t { Emission = 0, Dark, Reflection };

inline StarSubtype starSubtype(const ContentObject& o) noexcept { return static_cast<StarSubtype>(o.subtype); }
inline PlanetSubtype planetSubtype(const ContentObject& o) noexcept { return static_cast<PlanetSubtype>(o.subtype); }
inline StationSubtype stationSubtype(const ContentObject& o) noexcept { return static_cast<StationSubtype>(o.subtype); }
inline AsteroidSubtype asteroidSubtype(const ContentObject& o) noexcept { return static_cast<AsteroidSubtype>(o.subtype); }
inline ResourceSubtype resourceSubtype(const ContentObject& o) noexcept { return static_cast<ResourceSubtype>(o.subtype); }
inline NebulaSubtype nebulaSubtype(const ContentObject& o) noexcept { return static_cast<NebulaSubtype>(o.subtype); }

} // namespace starforge::content