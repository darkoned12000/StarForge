#pragma once

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "vulkan/vulkan_device.hpp"

#include <expected>
#include <mutex>
#include <string>

namespace starforge::gpu_resources
{

// One-shot buffer transfers with a cached, resettable command buffer and fence.
// No command buffer or fence is allocated per copy (M5 Phase 2.2).
class Uploader
{
public:
    [[nodiscard]] static std::expected<Uploader, std::string> create(const starforge::vulkan::VulkanDevice& device);

    ~Uploader();
    Uploader(Uploader&& other) noexcept;
    Uploader& operator=(Uploader&& other) noexcept;
    Uploader(const Uploader&) = delete;
    Uploader& operator=(const Uploader&) = delete;

    [[nodiscard]] std::expected<void, std::string> copyBuffer(const Buffer& src, Buffer& dst, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

    // Number of completed copy operations (for the allocation-thrashing tests).
    [[nodiscard]] std::uint64_t operationCount() const noexcept { return operationCount_; }

private:
    Uploader() = default;
    void destroy() noexcept;

    const starforge::vulkan::VulkanDevice* owner_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    std::mutex mutex_; // serialize use of the single cached cmd buffer/fence
    std::uint64_t operationCount_ = 0;
};

} // namespace starforge::gpu_resources
