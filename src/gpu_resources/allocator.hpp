#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge::gpu_resources
{

class Allocator
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
    };

    [[nodiscard]] static std::expected<Allocator, std::string> create(const CreateInfo& info);

    ~Allocator();
    Allocator(Allocator&& other) noexcept;
    Allocator& operator=(Allocator&& other) noexcept;
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    [[nodiscard]] VmaAllocator handle() const noexcept { return allocator_; }
    [[nodiscard]] std::uint32_t allocationCount() const noexcept;

    // Capture a VMA memory snapshot (allocated/used bytes, allocation count,
    // block count, heap usage). Safe to call at any time; does not affect
    // timed rendering measurements.
    struct MemorySnapshot
    {
        std::uint64_t allocatedBytes = 0;
        std::uint64_t usedBytes = 0;
        std::uint64_t allocationCount = 0;
        std::uint64_t blockCount = 0;
    };
    [[nodiscard]] MemorySnapshot captureMemorySnapshot() const noexcept;

private:
    Allocator() = default;
    void destroy() noexcept;

    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

} // namespace starforge::gpu_resources
