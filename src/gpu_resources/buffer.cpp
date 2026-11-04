#include "gpu_resources/buffer.hpp"

#include "vulkan/vulkan_result.hpp"

#include <utility>

namespace starforge::gpu_resources
{

namespace
{

VkBufferUsageFlags toVulkanUsage(BufferUsage usage)
{
    switch (usage)
    {
    case BufferUsage::TransferSrc:    return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    case BufferUsage::TransferDst:    return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    case BufferUsage::TransferSrcDst: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    case BufferUsage::Vertex:         return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    case BufferUsage::Index:          return VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    case BufferUsage::Uniform:        return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    case BufferUsage::Storage:        return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case BufferUsage::Indirect:       return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return 0;
}

VmaMemoryUsage toVmaUsage(MemoryPlacement memory)
{
    switch (memory)
    {
    case MemoryPlacement::GpuPreferred: return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    case MemoryPlacement::CpuToGpu:     return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    case MemoryPlacement::GpuToCpu:     return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }
    return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
}

VmaAllocationCreateFlags toVmaFlags(MemoryPlacement memory)
{
    switch (memory)
    {
    case MemoryPlacement::CpuToGpu:
        return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
    case MemoryPlacement::GpuToCpu:
        return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    case MemoryPlacement::GpuPreferred:
        return 0;
    }
    return 0;
}

} // namespace

std::expected<Buffer, std::string> Buffer::create(Allocator& allocator, const CreateInfo& info)
{
    if (info.size == 0)
    {
        return std::unexpected("Buffer size must be > 0");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = info.size;
    bufferInfo.usage = toVulkanUsage(info.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = toVmaUsage(info.memory);
    allocInfo.flags = toVmaFlags(info.memory);

    Buffer buffer;
    buffer.allocator_ = allocator.handle();
    buffer.size_ = info.size;

    VkResult result = vmaCreateBuffer(buffer.allocator_, &bufferInfo, &allocInfo, &buffer.buffer_, &buffer.allocation_, nullptr);
    if (result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vmaCreateBuffer", result));
    }

    if (info.name != nullptr)
    {
        vmaSetAllocationName(buffer.allocator_, buffer.allocation_, info.name);
    }

    return buffer;
}

Buffer::~Buffer()
{
    if (allocator_ != nullptr && buffer_ != VK_NULL_HANDLE)
    {
        if (mapped_)
        {
            vmaUnmapMemory(allocator_, allocation_);
        }
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }
    buffer_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
    mapped_ = nullptr;
    allocator_ = nullptr;
    size_ = 0;
}

Buffer::Buffer(Buffer&& other) noexcept
    : allocator_(std::exchange(other.allocator_, nullptr)),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, nullptr)),
      size_(other.size_),
      mapped_(std::exchange(other.mapped_, nullptr))
{
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other)
    {
        if (allocator_ != nullptr && buffer_ != VK_NULL_HANDLE)
        {
            if (mapped_)
            {
                vmaUnmapMemory(allocator_, allocation_);
            }
            vmaDestroyBuffer(allocator_, buffer_, allocation_);
        }
        allocator_ = std::exchange(other.allocator_, nullptr);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, nullptr);
        size_ = other.size_;
        mapped_ = std::exchange(other.mapped_, nullptr);
    }
    return *this;
}

std::expected<void*, std::string> Buffer::map()
{
    if (allocator_ == nullptr || buffer_ == VK_NULL_HANDLE)
    {
        return std::unexpected("Buffer not initialized");
    }
    if (mapped_)
    {
        return mapped_;
    }
    void* data = nullptr;
    VkResult result = vmaMapMemory(allocator_, allocation_, &data);
    if (result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vmaMapMemory", result));
    }
    mapped_ = data;
    return mapped_;
}

void Buffer::unmap()
{
    if (allocator_ != nullptr && allocation_ != nullptr && mapped_)
    {
        vmaUnmapMemory(allocator_, allocation_);
        mapped_ = nullptr;
    }
}

std::expected<void, std::string> Buffer::flush(VkDeviceSize offset, VkDeviceSize size)
{
    if (allocator_ == nullptr || allocation_ == nullptr)
    {
        return std::unexpected("Buffer not initialized");
    }
    VkResult result = vmaFlushAllocation(allocator_, allocation_, offset, size);
    if (result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vmaFlushAllocation", result));
    }
    return {};
}

std::expected<void, std::string> Buffer::invalidate(VkDeviceSize offset, VkDeviceSize size)
{
    if (allocator_ == nullptr || allocation_ == nullptr)
    {
        return std::unexpected("Buffer not initialized");
    }
    VkResult result = vmaInvalidateAllocation(allocator_, allocation_, offset, size);
    if (result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vmaInvalidateAllocation", result));
    }
    return {};
}

VkDeviceAddress Buffer::deviceAddress(VkDevice device) const noexcept
{
    if (allocator_ == nullptr || buffer_ == VK_NULL_HANDLE)
    {
        return 0;
    }
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer_;
    return vkGetBufferDeviceAddress(device, &info);
}

} // namespace starforge::gpu_resources