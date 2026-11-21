#include "gpu_resources/instance_data.hpp"
#include "vulkan/vulkan_device.hpp"
#include "gpu_resources/staging_ring_buffer.hpp"

namespace starforge::gpu_resources
{

InstanceBuffer::InstanceBuffer(Allocator& allocator) : allocator_(allocator)
{
    // Constructor - buffer will be initialized later with specific instance count
}

bool InstanceBuffer::initialize(std::size_t instanceCount)
{
    if (instanceCount == 0) return false;
    
    m_instanceCount = instanceCount;
    
    // Create the buffer for instance data 
    auto bufferResult = Buffer::create(allocator_, {
        .size = sizeof(InstanceData) * instanceCount,
        .usage = BufferUsage::Storage,
        .memory = MemoryPlacement::GpuPreferred,
    });
    
    if (!bufferResult.has_value()) return false;
    
    m_buffer = std::move(bufferResult.value());
    return true;
}

std::expected<void, std::string> InstanceBuffer::update(const std::vector<InstanceData>& instances)
{
    if (instances.empty())
    {
        return std::unexpected("InstanceBuffer::update: instances must not be empty");
    }
    if (m_buffer.handle() == VK_NULL_HANDLE || m_instanceCount == 0)
    {
        return std::unexpected("InstanceBuffer::update: buffer is not initialized");
    }
    if (instances.size() > m_instanceCount)
    {
        return std::unexpected("InstanceBuffer::update: instance count exceeds buffer capacity");
    }
    return std::unexpected("InstanceBuffer::update: GPU upload path is not implemented");
}

std::uint64_t InstanceBuffer::getBufferAddress(VkDevice device) const noexcept
{
    if (device == VK_NULL_HANDLE || m_buffer.handle() == VK_NULL_HANDLE)
    {
        return 0;
    }
    return m_buffer.deviceAddress(device);
}

std::size_t InstanceBuffer::getInstanceCount() const noexcept
{
    return m_instanceCount;
}

} // namespace starforge::gpu_resources