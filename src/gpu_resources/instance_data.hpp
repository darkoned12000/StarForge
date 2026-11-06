#pragma once

#include "gpu_resources/buffer.hpp"

#include <vector>
#include <cstdint>

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

// Contiguous buffer of full transforms/material indices accessed via BDA
class InstanceBuffer
{
public:
    explicit InstanceBuffer(Allocator& allocator);
    
    // Initialize with a specific number of instances
    bool initialize(std::size_t instanceCount);
    
    // Update the instance data for this frame  
    void update(const std::vector<InstanceData>& instances);
    
    // Get the buffer device address for shader access
    std::uint64_t getBufferAddress() const;
    
    // Get the number of instances in this buffer
    std::size_t getInstanceCount() const;

private:
    Allocator& allocator_;
    Buffer m_buffer;
    std::size_t m_instanceCount = 0;
};

} // namespace starforge::gpu_resources