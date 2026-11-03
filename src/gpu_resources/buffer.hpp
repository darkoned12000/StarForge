#pragma once

#include "gpu_resources/allocator.hpp"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

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
    void flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void invalidate(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

private:
    Allocator* allocator_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

} // namespace starforge::gpu_resources
