#pragma once

#include "gpu_resources/buffer.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::gpu_resources
{

// Instance data structure containing full transforms and material indices 
struct InstanceData
{
    // Full 4x4 transformation matrix (row-major order)
    float transform[16];
    
    // Material index for this instance  
    std::uint32_t materialIndex;
    
    // Padding to ensure proper alignment
    std::uint32_t padding[3];
};

// Contiguous buffer of full transforms/material indices accessed via BDA.
// NOTE: GPU upload path is not implemented yet; update() returns an explicit
// error so callers cannot silently assume data was uploaded.
class InstanceBuffer
{
public:
    explicit InstanceBuffer(Allocator& allocator);
    
    // Initialize with a specific number of instances.
    [[nodiscard]] bool initialize(std::size_t instanceCount);
    
    // Upload instance data for this frame. Not implemented yet: returns an
    // explicit error instead of silently dropping the update.
    [[nodiscard]] std::expected<void, std::string> update(const std::vector<InstanceData>& instances);
    
    // Get the buffer device address for shader access
    [[nodiscard]] std::uint64_t getBufferAddress(VkDevice device) const noexcept;
    
    // Get the number of instances in this buffer
    [[nodiscard]] std::size_t getInstanceCount() const noexcept;

private:
    Allocator& allocator_;
    Buffer m_buffer;
    std::size_t m_instanceCount = 0;
};

} // namespace starforge::gpu_resources