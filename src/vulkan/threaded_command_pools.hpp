#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace starforge::vulkan
{

class ThreadedCommandPools
{
public:
    ThreadedCommandPools() = default;
    ~ThreadedCommandPools();

    ThreadedCommandPools(ThreadedCommandPools&& other) noexcept;
    ThreadedCommandPools& operator=(ThreadedCommandPools&& other) noexcept;
    ThreadedCommandPools(const ThreadedCommandPools&) = delete;
    ThreadedCommandPools& operator=(const ThreadedCommandPools&) = delete;

    [[nodiscard]] std::expected<void, std::string> init(
        VkDevice device,
        std::uint32_t queueFamily,
        std::uint32_t threadCount,
        std::uint32_t framesInFlight);

    [[nodiscard]] VkCommandPool pool(std::uint32_t threadIndex, std::uint32_t frameIndex) const noexcept;

    void resetFrame(std::uint32_t frameIndex) noexcept;

    void destroy() noexcept;

    [[nodiscard]] std::uint32_t threadCount() const noexcept { return threadCount_; }
    [[nodiscard]] std::uint32_t framesInFlight() const noexcept { return framesInFlight_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t threadCount_ = 0;
    std::uint32_t framesInFlight_ = 0;
    std::vector<VkCommandPool> pools_;
};

} // namespace starforge::vulkan
