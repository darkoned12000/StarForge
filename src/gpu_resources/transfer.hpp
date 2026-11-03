#pragma once

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "vulkan/vulkan_device.hpp"

#include <expected>
#include <string>
#include <vector>

namespace starforge::gpu_resources
{

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

private:
    Uploader() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
};

} // namespace starforge::gpu_resources
