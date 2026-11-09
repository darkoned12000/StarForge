#pragma once

#include "content/cell_traversal.hpp"
#include "content/content_generation.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace starforge::content
{

// Immutable raw generation cache tier (`seed → ContentChunk`).
//
// Entries are LRU-evicted by entry count or estimated retained bytes.
// On a hit the same shared, immutable chunk is returned; on a miss the
// chunk is generated once through the budgeted path and inserted.
// Eviction never invalidates shared pointers already handed to callers.
// All data structures are guarded by a mutex, so concurrent readers
// (streaming threads, job workers) are safe.
class RawGenerationStore
{
public:
    struct Stats
    {
        std::size_t lookups = 0;
        std::size_t hits = 0;
        std::size_t misses = 0;
        std::size_t evictions = 0;
    };

    struct Config
    {
        std::size_t maxEntries = 256;
        std::size_t maxBytes = 64 * 1024 * 1024;
    };

    explicit RawGenerationStore(Config config) noexcept;

    [[nodiscard]] std::shared_ptr<const ContentChunk> get(std::uint64_t seed, const CellCoord& cell,
                                                          const GenerationBudget& budget = {});
    [[nodiscard]] bool contains(std::uint64_t seed, const CellCoord& cell) const;
    void clear() noexcept;

    [[nodiscard]] std::size_t entryCount() const noexcept;
    [[nodiscard]] std::size_t retainedBytes() const noexcept;
    [[nodiscard]] Stats stats() const noexcept;

private:
    struct Key
    {
        std::uint64_t seed = 0;
        std::int64_t x = 0;
        std::int64_t y = 0;
        std::int64_t z = 0;
        int level = 0;

        friend bool operator==(const Key&, const Key&) noexcept = default;
    };

    struct Entry
    {
        std::shared_ptr<const ContentChunk> chunk;
        std::size_t bytes = 0;
        std::list<Key>::iterator orderIt;
    };

    struct KeyHash
    {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept;
    };

    [[nodiscard]] static std::size_t estimatedBytes(const ContentChunk& chunk) noexcept;
    void evictIfNeeded() noexcept;

    Config config_;
    Stats stats_;
    std::list<Key> order_;
    std::unordered_map<Key, Entry, KeyHash> entries_;
    std::size_t retainedBytes_ = 0;
    mutable std::mutex mutex_;
};

} // namespace starforge::content
