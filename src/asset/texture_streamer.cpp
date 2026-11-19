#include "texture_streamer.hpp"

#include <algorithm>
#include <utility>

namespace starforge::asset
{

namespace
{
TextureData downsample(const TextureData& source)
{
    TextureData out;
    if (source.width == 0 || source.height == 0 || source.rgba.empty()) return out;
    out.width = std::max<std::uint32_t>(1, source.width / 2);
    out.height = std::max<std::uint32_t>(1, source.height / 2);
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (std::uint32_t y = 0; y < out.height; ++y)
        for (std::uint32_t x = 0; x < out.width; ++x)
        {
            const std::uint32_t sx = std::min(source.width - 1, x * 2);
            const std::uint32_t sy = std::min(source.height - 1, y * 2);
            for (std::uint32_t channel = 0; channel < 4; ++channel)
            {
                unsigned int sum = 0;
                unsigned int samples = 0;
                for (std::uint32_t dy = 0; dy < 2; ++dy)
                    for (std::uint32_t dx = 0; dx < 2; ++dx)
                    {
                        const std::uint32_t px = std::min(source.width - 1, sx + dx);
                        const std::uint32_t py = std::min(source.height - 1, sy + dy);
                        sum += source.rgba[(static_cast<std::size_t>(py) * source.width + px) * 4 + channel];
                        ++samples;
                    }
                const auto index = (static_cast<std::size_t>(y) * out.width + x) * 4 + channel;
                out.rgba[index] = static_cast<std::uint8_t>(sum / samples);
            }
        }
    return out;
}
} // namespace

TextureMipChain::TextureMipChain(TextureData base)
{
    if (base.width == 0 || base.height == 0 || base.rgba.empty()) return;
    levels_.push_back(std::move(base));
    while (levels_.back().width > 1 || levels_.back().height > 1)
    {
        auto next = downsample(levels_.back());
        if (next.width == levels_.back().width && next.height == levels_.back().height) break;
        levels_.push_back(std::move(next));
    }
}

const TextureData& TextureMipChain::level(std::size_t index) const noexcept
{
    static const TextureData empty;
    return index < levels_.size() ? levels_[index] : empty;
}

std::size_t TextureMipChain::totalBytes() const noexcept
{
    std::size_t total = 0;
    for (const auto& level : levels_) total += level.rgba.size();
    return total;
}

std::size_t TextureMipChain::selectLevel(std::uint32_t screenPixels) const noexcept
{
    if (levels_.empty()) return 0;
    std::size_t level = 0;
    while (level + 1 < levels_.size() &&
           std::max(levels_[level].width, levels_[level].height) > screenPixels)
        ++level;
    return level;
}

StreamingTexture::StreamingTexture(TextureData base, std::size_t budgetBytes)
    : StreamingTexture(TextureMipChain(std::move(base)), budgetBytes)
{
}

StreamingTexture::StreamingTexture(TextureMipChain chain, std::size_t budgetBytes)
    : chain_(std::move(chain)), budget_(budgetBytes)
{
}

void StreamingTexture::setBudget(std::size_t bytes) noexcept
{
    budget_ = bytes;
}

void StreamingTexture::touch(std::size_t level) noexcept
{
    for (auto it = lru_.begin(); it != lru_.end(); ++it)
        if (it->second == level)
        {
            lru_.erase(it);
            break;
        }
    lru_.emplace(counter_++, level);
}

const TextureData* StreamingTexture::request(std::size_t level) noexcept
{
    if (level >= mipCount()) return nullptr;
    const auto found = resident_.find(level);
    if (found != resident_.end())
    {
        touch(level);
        return &found->second;
    }

    const auto& source = chain_.level(level);
    const std::size_t bytes = source.rgba.size();
    while (!resident_.empty() && residentBytes_ + bytes > budget_)
    {
        const auto oldest = lru_.begin();
        if (oldest == lru_.end()) break;
        const auto victim = resident_.find(oldest->second);
        if (victim == resident_.end())
        {
            lru_.erase(oldest);
            continue;
        }
        residentBytes_ = residentBytes_ >= victim->second.rgba.size()
            ? residentBytes_ - victim->second.rgba.size() : 0;
        resident_.erase(victim);
        lru_.erase(oldest);
    }

    resident_.emplace(level, source);
    residentBytes_ += bytes;
    touch(level);
    return &resident_.find(level)->second;
}

const TextureData* StreamingTexture::data(std::size_t level) const noexcept
{
    const auto it = resident_.find(level);
    return it == resident_.end() ? nullptr : &it->second;
}

bool StreamingTexture::resident(std::size_t level) const noexcept
{
    return resident_.find(level) != resident_.end();
}

bool StreamingTexture::evict(std::size_t level) noexcept
{
    const auto it = resident_.find(level);
    if (it == resident_.end()) return false;
    residentBytes_ = residentBytes_ >= it->second.rgba.size()
        ? residentBytes_ - it->second.rgba.size() : 0;
    resident_.erase(it);
    for (auto lruIt = lru_.begin(); lruIt != lru_.end(); ++lruIt)
        if (lruIt->second == level)
        {
            lru_.erase(lruIt);
            break;
        }
    return true;
}

void StreamingTexture::evictAll() noexcept
{
    resident_.clear();
    lru_.clear();
    residentBytes_ = 0;
}

const TextureData* StreamingTexture::stream(std::uint32_t screenPixels) noexcept
{
    return request(chain_.selectLevel(screenPixels));
}

} // namespace starforge::asset
