#pragma once

// M17.5: CPU-side texture mip generation and budgeted residency. The toon
// renderer uploads packed RGBA8 through a storage buffer; streaming means a
// mip chain is generated once and only the needed levels are kept resident.

#include "gltf_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace starforge::asset
{

class TextureMipChain
{
public:
    TextureMipChain() noexcept = default;
    explicit TextureMipChain(TextureData base);
    [[nodiscard]] std::size_t mipCount() const noexcept { return levels_.size(); }
    [[nodiscard]] const TextureData& level(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t totalBytes() const noexcept;
    // Level whose largest dimension is closest to (but not above) the
    // requested screen footprint in pixels.
    [[nodiscard]] std::size_t selectLevel(std::uint32_t screenPixels) const noexcept;

private:
    std::vector<TextureData> levels_;
};

class StreamingTexture
{
public:
    StreamingTexture() noexcept = default;
    explicit StreamingTexture(TextureData base, std::size_t budgetBytes = 4u * 1024u * 1024u);
    explicit StreamingTexture(TextureMipChain chain, std::size_t budgetBytes = 4u * 1024u * 1024u);

    [[nodiscard]] const TextureMipChain& chain() const noexcept { return chain_; }
    [[nodiscard]] std::size_t mipCount() const noexcept { return chain_.mipCount(); }
    [[nodiscard]] std::size_t budget() const noexcept { return budget_; }
    void setBudget(std::size_t bytes) noexcept;
    [[nodiscard]] std::size_t residentBytes() const noexcept { return residentBytes_; }
    [[nodiscard]] std::size_t residentCount() const noexcept { return resident_.size(); }

    // Makes a level resident (evicting least-recently-used levels as needed)
    // and returns its pixels. A level larger than the whole budget is allowed.
    [[nodiscard]] const TextureData* request(std::size_t level) noexcept;
    [[nodiscard]] const TextureData* data(std::size_t level) const noexcept;
    [[nodiscard]] bool resident(std::size_t level) const noexcept;
    bool evict(std::size_t level) noexcept;
    void evictAll() noexcept;

    // Requests the mip appropriate for a screen footprint and returns it.
    [[nodiscard]] const TextureData* stream(std::uint32_t screenPixels) noexcept;

private:
    void touch(std::size_t level) noexcept;

    TextureMipChain chain_;
    std::size_t budget_ = 0;
    std::size_t residentBytes_ = 0;
    std::unordered_map<std::size_t, TextureData> resident_;
    std::map<std::uint64_t, std::size_t> lru_; // access counter -> level
    std::uint64_t counter_ = 0;
};

} // namespace starforge::asset
