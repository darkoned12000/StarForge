#pragma once

#include "jobs/job_scheduler.hpp"
#include "vulkan/threaded_command_pools.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace starforge::vulkan
{

// Configuration for parallel secondary command buffer recording.
struct SecondaryRecordingConfig
{
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkFormat colorFormat = VK_FORMAT_R8G8R8A8_UNORM;
    std::vector<VkFormat> colorFormats; // when non-empty, overrides colorFormat
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t maxThreads = 4;      // one pool slot per concurrent recording job
    std::uint32_t framesInFlight = 2;
};

// A single secondary recording job. Each task gets its own per-thread command
// pool slot, so tasks may run concurrently on any worker thread without
// sharing a VkCommandPool (Vulkan requires external sync per pool).
struct SecondaryTask
{
    void (*fn)(VkCommandBuffer secondary, void* userData, std::uint32_t slot) = nullptr;
    void* userData = nullptr;
};

// 5.5.5d: records multiple secondaries concurrently through the job system
// with dynamic-rendering inheritance. The returned command buffers are valid
// until resetFrame(frameIndex) is called or the recorder is destroyed.
class ParallelSecondaryRecorder
{
public:
    static std::expected<ParallelSecondaryRecorder, std::string>
    create(const SecondaryRecordingConfig& config);

    ~ParallelSecondaryRecorder() = default;
    ParallelSecondaryRecorder(ParallelSecondaryRecorder&&) noexcept = default;
    ParallelSecondaryRecorder& operator=(ParallelSecondaryRecorder&&) noexcept = default;
    ParallelSecondaryRecorder(const ParallelSecondaryRecorder&) = delete;
    ParallelSecondaryRecorder& operator=(const ParallelSecondaryRecorder&) = delete;

    [[nodiscard]] std::expected<std::vector<VkCommandBuffer>, std::string>
    record(std::uint32_t frameIndex,
           std::span<const SecondaryTask> tasks,
           jobs::JobScheduler& scheduler);

    void resetFrame(std::uint32_t frameIndex) noexcept { pools_.resetFrame(frameIndex); }

    [[nodiscard]] const SecondaryRecordingConfig& config() const noexcept { return config_; }
    [[nodiscard]] const std::vector<std::thread::id>& lastRecordingThreads() const noexcept
    {
        return lastRecordingThreads_;
    }

private:
    ParallelSecondaryRecorder() = default;

    SecondaryRecordingConfig config_;
    ThreadedCommandPools pools_;
    std::vector<std::thread::id> lastRecordingThreads_;
};

} // namespace starforge::vulkan
