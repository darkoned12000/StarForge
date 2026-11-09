#include "content/chunk_cache.hpp"
#include "content/hash.hpp"

#include <algorithm>
#include <utility>

namespace starforge::content
{

RawGenerationStore::RawGenerationStore(Config config) noexcept : config_(config)
{
    if (config_.maxEntries == 0)
    {
        config_.maxEntries = 1;
    }
    if (config_.maxBytes == 0)
    {
        config_.maxBytes = 1;
    }
}

std::shared_ptr<const ContentChunk> RawGenerationStore::get(std::uint64_t seed, const CellCoord& cell,
                                                            const GenerationBudget& budget)
{
    const Key key{seed, cell.x, cell.y, cell.z, cell.level};
    {
        std::lock_guard lock(mutex_);
        ++stats_.lookups;
        const auto found = entries_.find(key);
        if (found != entries_.end())
        {
            ++stats_.hits;
            if (found->second.orderIt != order_.begin())
            {
                order_.splice(order_.begin(), order_, found->second.orderIt);
            }
            return found->second.chunk;
        }
        ++stats_.misses;
    }

    // Generate without the lock so concurrent workers don't serialize.
    const ContentChunk generated = generateContentBudgeted(seed, cell.x, cell.y, cell.z, cell.level, budget);
    const std::size_t bytes = estimatedBytes(generated);
    auto chunk = std::make_shared<const ContentChunk>(std::move(generated));

    std::lock_guard lock(mutex_);
    const auto inserted = entries_.find(key);
    if (inserted != entries_.end())
    {
        // Another worker won the race; return the resident chunk.
        ++stats_.hits;
        if (inserted->second.orderIt != order_.begin())
        {
            order_.splice(order_.begin(), order_, inserted->second.orderIt);
        }
        return inserted->second.chunk;
    }

    if (bytes <= config_.maxBytes)
    {
        order_.push_front(key);
        entries_.emplace(key, Entry{chunk, bytes, order_.begin()});
        retainedBytes_ += bytes;
        evictIfNeeded();
    }
    return chunk;
}

bool RawGenerationStore::contains(std::uint64_t seed, const CellCoord& cell) const
{
    std::lock_guard lock(mutex_);
    const Key key{seed, cell.x, cell.y, cell.z, cell.level};
    return entries_.find(key) != entries_.end();
}

void RawGenerationStore::clear() noexcept
{
    std::lock_guard lock(mutex_);
    retainedBytes_ = 0;
    entries_.clear();
    order_.clear();
}

std::size_t RawGenerationStore::entryCount() const noexcept
{
    std::lock_guard lock(mutex_);
    return entries_.size();
}

std::size_t RawGenerationStore::retainedBytes() const noexcept
{
    std::lock_guard lock(mutex_);
    return retainedBytes_;
}

RawGenerationStore::Stats RawGenerationStore::stats() const noexcept
{
    std::lock_guard lock(mutex_);
    return stats_;
}

std::size_t RawGenerationStore::estimatedBytes(const ContentChunk& chunk) noexcept
{
    return sizeof(ContentChunk) + chunk.objects.size() * sizeof(ContentObject);
}

void RawGenerationStore::evictIfNeeded() noexcept
{
    while (entries_.size() > config_.maxEntries || retainedBytes_ > config_.maxBytes)
    {
        if (order_.empty())
        {
            return;
        }
        const Key victim = order_.back();
        order_.pop_back();
        const auto found = entries_.find(victim);
        if (found == entries_.end())
        {
            continue;
        }
        retainedBytes_ -= found->second.bytes;
        entries_.erase(found);
        ++stats_.evictions;
    }
}

std::size_t RawGenerationStore::KeyHash::operator()(const Key& key) const noexcept
{
    return static_cast<std::size_t>(contentSeed(key.seed, key.x, key.y, key.z, key.level));
}

} // namespace starforge::content
