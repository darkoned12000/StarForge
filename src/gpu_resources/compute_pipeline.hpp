#pragma once

#include "gpu_resources/buffer.hpp"
#include "vulkan/vulkan_device.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>

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
    // waits for completion with a memory barrier making shader writes visible.
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

    // Record a dispatch into an existing command buffer (no submit, no wait).
    // Caller is responsible for barriers before/after.
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

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    std::uint32_t storageBufferCount_ = 0;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
};

} // namespace starforge::gpu_resources
