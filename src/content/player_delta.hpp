#pragma once

#include "content/content_generation.hpp"
#include "content/hash.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace starforge::content
{

// Type of player modification to generated content.
enum class DeltaType : std::uint8_t
{
    Removed,     // Object destroyed/removed
    Modified,    // Object properties changed (scale, position, subtype)
    Added,       // New object added by player
};

// A player modification to a generated content object.
struct PlayerDelta
{
    // Cell coordinates (identifies which cell this delta applies to).
    std::int64_t cellX = 0;
    std::int64_t cellY = 0;
    std::int64_t cellZ = 0;
    int level = 0;

    // Object identification.
    std::uint64_t objectId = 0;

    // Modification type.
    DeltaType type = DeltaType::Removed;

    // Modified properties (only used for Modified/Added).
    float scale = 1.0f;
    std::uint32_t subtype = 0;
    std::uint32_t properties = 0;
    double localX = 0.0;
    double localY = 0.0;
    double localZ = 0.0;
    ContentType objectType = ContentType::Asteroid;

    // Unique key for this delta.
    [[nodiscard]] std::uint64_t key() const noexcept;
};

// Collision-free cell key for per-cell indexing.
// Uses the coordinates directly (not a hash) to guarantee no collisions.
struct CellKey
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    int level = 0;

    auto operator<=>(const CellKey&) const noexcept = default;
};

struct CellKeyHash
{
    std::uint64_t operator()(const CellKey& k) const noexcept
    {
        // splitMix64-style mixing on the full coordinate set.
        std::uint64_t h = static_cast<std::uint64_t>(k.x) ^ 0x9e3779b97f4a7c15ULL;
        h ^= static_cast<std::uint64_t>(k.y) * 0xbf58476d1ce4e5b9ULL;
        h ^= static_cast<std::uint64_t>(k.z) * 0x94d049bb133111b9ULL;
        h ^= static_cast<std::uint64_t>(k.level) * 0xff51afd7ed558ccdULL;
        h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
        h = (h ^ (h >> 27)) * 0x94d049bb133111b9ULL;
        return h ^ (h >> 31);
    }
};

// Collision-free key identifying a specific object in a specific cell.
struct DeltaKey
{
    std::int64_t cellX = 0;
    std::int64_t cellY = 0;
    std::int64_t cellZ = 0;
    int level = 0;
    std::uint64_t objectId = 0;

    auto operator<=>(const DeltaKey&) const noexcept = default;
};

struct DeltaKeyHash
{
    std::uint64_t operator()(const DeltaKey& k) const noexcept
    {
        std::uint64_t h = hashCell(k.cellX, k.cellY, k.cellZ);
        h ^= static_cast<std::uint64_t>(k.level) * 0x9e3779b97f4a7c15ULL;
        h ^= k.objectId * 0xbf58476d1ce4e5b9ULL;
        h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
        h = (h ^ (h >> 27)) * 0x94d049bb133111b9ULL;
        return h ^ (h >> 31);
    }
};

// Sparse storage for player deltas, keyed by (cell, objectId).
class DeltaStore
{
public:
    DeltaStore() = default;

    // Apply a delta (add or replace).
    void apply(const PlayerDelta& delta);

    // Query deltas for a specific cell.
    [[nodiscard]] std::vector<PlayerDelta> deltasForCell(
        std::int64_t cx, std::int64_t cy, std::int64_t cz, int level) const;

    // Check if an object has been removed.
    [[nodiscard]] bool isRemoved(
        std::int64_t cx, std::int64_t cy, std::int64_t cz, int level,
        std::uint64_t objectId) const;

    // Get a specific delta (if exists).
    [[nodiscard]] const PlayerDelta* getDelta(
        std::int64_t cx, std::int64_t cy, std::int64_t cz, int level,
        std::uint64_t objectId) const;

    // Number of stored deltas.
    [[nodiscard]] std::size_t size() const noexcept { return deltas_.size(); }

    // Clear all deltas.
    void clear() noexcept
    {
        deltas_.clear();
        cellIndex_.clear();
    }

    // Serialize to bytes (little-endian, platform-independent).
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;

    // Deserialize from bytes. Clears existing contents first.
    // Returns number of deltas loaded.
    [[nodiscard]] std::size_t deserialize(const std::vector<std::uint8_t>& data);

private:
    // Primary store: keyed by collision-free (cell, objectId).
    std::unordered_map<DeltaKey, PlayerDelta, DeltaKeyHash> deltas_;
    // Per-cell index for fast deltasForCell.
    std::unordered_map<CellKey, std::vector<DeltaKey>, CellKeyHash> cellIndex_;
};

// Apply player deltas to a generated content chunk.
// Returns a new chunk with deltas applied (removed objects filtered out,
// modified objects updated, added objects appended).
[[nodiscard]] ContentChunk applyDeltas(
    const ContentChunk& generated,
    const DeltaStore& store);

} // namespace starforge::content