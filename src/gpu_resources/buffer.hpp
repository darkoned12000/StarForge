#pragma once

#include "gpu_resources/allocator.hpp"

#include <vulkan/vulkan.h>
// VMA's intended configuration: disable Clang-specific nullability attributes.
#define VMA_NULLABLE
#define VMA_NOT_NULL
#define VMA_NULLABLE_NON_DISPATCHABLE VMA_NULLABLE
#define VMA_NOT_NULL_NON_DISPATCHABLE VMA_NOT_NULL
#include <vk_mem_alloc.h>

#include <cstddef>
#include <expected>
#include <string>

namespace starforge::gpu_resources
{

enum class BufferUsage
{
    TransferSrc,
    TransferDst,
    TransferSrcDst,
    Vertex,
    Index,
    Uniform,
    Storage,
    Indirect,
};

enum class MemoryPlacement
{
    GpuPreferred,
    CpuToGpu,
    GpuToCpu,
};

class Buffer
{
public:
    struct CreateInfo
    {
        VkDeviceSize size = 0;
        BufferUsage usage = BufferUsage::Storage;
        MemoryPlacement memory = MemoryPlacement::GpuPreferred;
        const char* name = nullptr;
    };

    Buffer() = default;
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    [[nodiscard]] static std::expected<Buffer, std::string>
    create(Allocator& allocator, const CreateInfo& info);

    [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
    [[nodiscard]] VmaAllocation allocation() const noexcept { return allocation_; }
    [[nodiscard]] VkDeviceSize size() const noexcept { return size_; }
    [[nodiscard]] bool mapped() const noexcept { return mapped_ != nullptr; }

    [[nodiscard]] std::expected<void*, std::string> map();
    void unmap();
    [[nodiscard]] std::expected<void, std::string> flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    [[nodiscard]] std::expected<void, std::string> invalidate(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    [[nodiscard]] VkDeviceAddress deviceAddress(VkDevice device) const noexcept;

private:
    VmaAllocator allocator_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

} // namespace starforge::gpu_resources
