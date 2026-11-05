#include "gpu_resources/staging_ring_buffer.hpp"

#include <utility>

namespace starforge::gpu_resources
{

StagingRingBuffer::StagingRingBuffer(StagingRingBuffer&& other) noexcept
    : buffer_(std::move(other.buffer_)),
      mappedData_(std::exchange(other.mappedData_, nullptr)),
      capacity_(std::exchange(other.capacity_, 0)),
      writeOffset_(other.writeOffset_.load(std::memory_order_relaxed))
{
}

StagingRingBuffer& StagingRingBuffer::operator=(StagingRingBuffer&& other) noexcept
{
    if (this != &other)
    {
        buffer_ = std::move(other.buffer_);
        mappedData_ = std::exchange(other.mappedData_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
        writeOffset_.store(other.writeOffset_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

std::expected<void, std::string> StagingRingBuffer::init(
    Allocator& allocator,
    std::size_t capacity)
{
    capacity_ = capacity;
    writeOffset_.store(0, std::memory_order_relaxed);

    Buffer::CreateInfo info{};
    info.size = capacity;
    info.usage = BufferUsage::TransferSrc;
    info.memory = MemoryPlacement::CpuToGpu;

    auto result = Buffer::create(allocator, info);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    buffer_ = std::move(*result);

    auto mapResult = buffer_.map();
    if (!mapResult)
    {
        return std::unexpected(mapResult.error());
    }
    mappedData_ = static_cast<std::uint8_t*>(*mapResult);

    return {};
}

std::optional<StagingAllocation> StagingRingBuffer::reserve(std::size_t size, std::size_t alignment)
{
    if (size == 0 || size > capacity_) return std::nullopt;

    std::size_t current = writeOffset_.load(std::memory_order_relaxed);
    std::size_t aligned = 0;
    std::size_t newOffset = 0;

    do
    {
        aligned = (current + alignment - 1) & ~(alignment - 1);
        if (aligned >= capacity_ || size > capacity_ - aligned)
        {
            return std::nullopt;
        }
        newOffset = aligned + size;
    } while (!writeOffset_.compare_exchange_weak(current, newOffset,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed));

    return StagingAllocation{
        .offset = aligned,
        .hostPtr = mappedData_ + aligned,
    };
}

void StagingRingBuffer::flush(std::size_t offset, std::size_t size)
{
    (void)buffer_.flush(offset, size);
}

void StagingRingBuffer::reset() noexcept
{
    writeOffset_.store(0, std::memory_order_release);
}

} // namespace starforge::gpu_resources
