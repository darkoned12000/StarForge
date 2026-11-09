#include <doctest/doctest.h>

#include "content/chunk_cache.hpp"

using namespace starforge::content;

TEST_CASE("Chunk cache: misses generate, hits reuse, replays stay deterministic")
{
    RawGenerationStore store(RawGenerationStore::Config{.maxEntries = 3, .maxBytes = 1024 * 1024});
    const CellCoord cell{7, -2, 3, 0};

    const auto first = store.get(42, cell);
    CHECK(store.stats().lookups == 1);
    CHECK(store.stats().misses == 1);
    CHECK(store.stats().hits == 0);

    const auto second = store.get(42, cell);
    CHECK(second == first);
    CHECK(store.stats().hits == 1);
    CHECK(store.stats().misses == 1);
    CHECK(store.contains(42, cell));
    CHECK(store.entryCount() == 1);
    CHECK(store.retainedBytes() == sizeof(ContentChunk) + first->objects.size() * sizeof(ContentObject));
}

TEST_CASE("Chunk cache: key separates seeds, levels, and cells")
{
    RawGenerationStore store(RawGenerationStore::Config{.maxEntries = 16, .maxBytes = 1024 * 1024});
    const auto a = store.get(42, {0, 0, 0, 0});
    const auto b = store.get(43, {0, 0, 0, 0});
    const auto c = store.get(42, {0, 0, 0, 1});
    CHECK(a != b);
    CHECK(a != c);
    CHECK(store.entryCount() == 3);
    CHECK(store.stats().misses == 3);
}

TEST_CASE("Chunk cache: LRU evicts the least recently used entry")
{
    RawGenerationStore store(RawGenerationStore::Config{.maxEntries = 2, .maxBytes = 1024 * 1024});
    (void)store.get(42, {0, 0, 0, 0});
    (void)store.get(42, {1, 0, 0, 0});
    // Refreshing 0 makes 1 the least recently used.
    (void)store.get(42, {0, 0, 0, 0});
    (void)store.get(42, {2, 0, 0, 0});

    CHECK(store.entryCount() == 2);
    CHECK(store.contains(42, {0, 0, 0, 0}));
    CHECK(store.contains(42, {2, 0, 0, 0}));
    CHECK(!store.contains(42, {1, 0, 0, 0}));
    CHECK(store.stats().evictions == 1);
}

TEST_CASE("Chunk cache: byte cap evicts under the memory budget")
{
    RawGenerationStore store(RawGenerationStore::Config{.maxEntries = 1024, .maxBytes = 1});       // one chunk cannot be retained
    const auto first = store.get(42, {0, 0, 0, 0});
    CHECK(first != nullptr);
    CHECK(store.entryCount() == 0);
    CHECK(store.retainedBytes() == 0);
    // The generated chunk is still usable even when it cannot be cached.
    REQUIRE(first->objects.size() > 0);
}
