#pragma once

#include "gpu_resources/buffer.hpp"
#include "vulkan/vulkan_device.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace starforge::gpu_resources
{

// Owns a compute shader module, descriptor layout/pool/set, pipeline layout,
// pipeline, and transient command pool for one-shot compute dispatches.
// Move-only; all Vulkan objects are released in the destructor.
class ComputePipeline
{
public:
    struct CreateInfo
    {
        std::span<const std::uint8_t> spirv;
        const char* entryPoint = "main";
        std::uint32_t storageBufferCount = 1;
        std::uint32_t pushConstantSize = 0; // bytes of push constants (0 = none)
    };

    [[nodiscard]] static std::expected<ComputePipeline, std::string>
    create(const starforge::vulkan::VulkanDevice& device, const CreateInfo& info);

    [[nodiscard]] static std::expected<ComputePipeline, std::string>
    createFromFile(const starforge::vulkan::VulkanDevice& device, const std::string& path,
                   const char* entryPoint = "main", std::uint32_t storageBufferCount = 1,
                   std::uint32_t pushConstantSize = 0);

    ~ComputePipeline();
    ComputePipeline(ComputePipeline&& other) noexcept;
    ComputePipeline& operator=(ComputePipeline&& other) noexcept;
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    // Binds the storage buffers to bindings 0..N-1, dispatches workgroups, and
    // waits for completion. Uses conservative barriers for correctness in
    // generic one-shot use; performance-sensitive paths should prefer
    // recordDispatch() with caller-managed barriers.
    [[nodiscard]] std::expected<void, std::string>
    dispatchAndWait(std::span<const Buffer* const> storageBuffers,
                    std::uint32_t groupCountX,
                    std::uint32_t groupCountY = 1,
                    std::uint32_t groupCountZ = 1);

    // Same as dispatchAndWait but also pushes push constant data.
    [[nodiscard]] std::expected<void, std::string>
    dispatchAndWaitWithPushConstants(std::span<const Buffer* const> storageBuffers,
                                    const void* pushData,
                                    std::uint32_t pushSize,
                                    std::uint32_t groupCountX,
                                    std::uint32_t groupCountY = 1,
                                    std::uint32_t groupCountZ = 1);

    // Bind storage buffers into the descriptor set once. Repeated dispatches
    // with the same buffer handles skip vkUpdateDescriptorSets entirely.
    [[nodiscard]] std::expected<void, std::string>
    bind(std::span<const Buffer* const> storageBuffers);

    // Number of completed one-shot dispatches (for allocation-thrashing tests).
    [[nodiscard]] std::uint64_t operationCount() const noexcept { return operationCount_; }
    // Number of descriptor-set updates actually issued (setup-phase proof).
    [[nodiscard]] std::uint64_t descriptorUpdateCount() const noexcept { return descriptorUpdateCount_; }

    // Record a dispatch into an existing command buffer (no submit, no wait).
    // Caller is responsible for barriers before/after and for ensuring the
    // descriptor set is not updated while command buffers that bind it are
    // pending execution.
    [[nodiscard]] std::expected<void, std::string>
    recordDispatch(VkCommandBuffer cmd,
                  std::span<const Buffer* const> storageBuffers,
                  const void* pushData,
                  std::uint32_t pushSize,
                  std::uint32_t groupCountX,
                  std::uint32_t groupCountY = 1,
                  std::uint32_t groupCountZ = 1);

private:
    ComputePipeline() = default;
    void destroy() noexcept;

    const starforge::vulkan::VulkanDevice* owner_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    std::uint32_t storageBufferCount_ = 0;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    std::mutex mutex_;            // serialize the cached cmd buffer/fence
    std::mutex descriptorMutex_;  // serialize descriptor updates
    std::uint64_t operationCount_ = 0;
    std::uint64_t descriptorUpdateCount_ = 0;
    std::vector<VkBuffer> boundBuffers_; // handles currently written into the set
};

} // namespace starforge::gpu_resources
