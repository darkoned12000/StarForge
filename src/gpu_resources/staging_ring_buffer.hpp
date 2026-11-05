#pragma once

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

namespace starforge::gpu_resources
{

struct StagingAllocation
{
    std::size_t offset = 0;
    std::uint8_t* hostPtr = nullptr;
};

class StagingRingBuffer
{
public:
    StagingRingBuffer() = default;
    ~StagingRingBuffer() = default;

    StagingRingBuffer(StagingRingBuffer&& other) noexcept;
    StagingRingBuffer& operator=(StagingRingBuffer&& other) noexcept;
    StagingRingBuffer(const StagingRingBuffer&) = delete;
    StagingRingBuffer& operator=(const StagingRingBuffer&) = delete;

    [[nodiscard]] std::expected<void, std::string> init(
        Allocator& allocator,
        std::size_t capacity);

    [[nodiscard]] std::optional<StagingAllocation> reserve(std::size_t size, std::size_t alignment = 256);

    void flush(std::size_t offset = 0, std::size_t size = VK_WHOLE_SIZE);

    [[nodiscard]] Buffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] const Buffer& buffer() const noexcept { return buffer_; }
    [[nodiscard]] std::uint8_t* mappedBase() noexcept { return mappedData_; }

    void reset() noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t used() const noexcept { return writeOffset_.load(std::memory_order_relaxed); }

private:
    Buffer buffer_;
    std::uint8_t* mappedData_ = nullptr;
    std::size_t capacity_ = 0;
    std::atomic<std::size_t> writeOffset_{0};
};

} // namespace starforge::gpu_resources
