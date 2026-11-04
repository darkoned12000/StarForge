#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::vulkan
{

// GPU timestamp query system with per-frame query pools and named scopes.
// Uses synchronization2 (vkCmdWriteTimestamp2) and VK_EXT_debug_utils labels.
// Results are collected asynchronously to avoid per-frame GPU stalls.
class GpuTimer
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        std::uint32_t framesInFlight = 2;
        std::uint32_t maxScopesPerFrame = 16;
    };

    [[nodiscard]] static std::expected<GpuTimer, std::string> create(const CreateInfo& info);

    ~GpuTimer();
    GpuTimer(GpuTimer&& other) noexcept;
    GpuTimer& operator=(GpuTimer&& other) noexcept;
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    // Whether GPU timestamps are supported on the selected queue.
    [[nodiscard]] bool supported() const noexcept { return timestampValidBits_ > 0; }
    [[nodiscard]] std::uint32_t validBits() const noexcept { return timestampValidBits_; }
    [[nodiscard]] double periodNs() const noexcept { return timestampPeriodNs_; }
    [[nodiscard]] std::uint32_t maxScopes() const noexcept { return maxScopesPerFrame_; }

    // Query pool for a given frame (for vkCmdResetQueryPool / vkCmdWriteTimestamp2).
    [[nodiscard]] VkQueryPool queryPool(std::uint32_t frameIndex) const;

    // Reset the query pool for a frame. Call at the start of command recording.
    void resetQueryPool(VkCommandBuffer cmd, std::uint32_t frameIndex, std::uint32_t queryCount);

    // Write a timestamp. stage is the pipeline stage at which to capture.
    void writeTimestamp(VkCommandBuffer cmd, std::uint32_t frameIndex,
                       std::uint32_t queryIndex, VkPipelineStageFlags2 stage);

    // Push/pop a debug label on the command buffer.
    void pushLabel(VkCommandBuffer cmd, const char* name);
    void popLabel(VkCommandBuffer cmd);

    // Begin a named scope: push debug label + write begin timestamp.
    // Returns the query index for the begin timestamp.
    std::uint32_t beginScope(VkCommandBuffer cmd, std::uint32_t frameIndex, const char* name);

    // End the current scope: write end timestamp + pop debug label.
    // Returns the query index for the end timestamp.
    std::uint32_t endScope(VkCommandBuffer cmd, std::uint32_t frameIndex);

    // Collect results for a completed frame (non-blocking).
    // Returns true if results are available.
    bool collectResults(std::uint32_t frameIndex);

    // Get duration in nanoseconds between two query indices for a frame.
    // Returns -1.0 if not available.
    double durationNs(std::uint32_t frameIndex, std::uint32_t beginQuery,
                      std::uint32_t endQuery) const;

    // Get the number of scopes recorded for a frame.
    std::uint32_t scopeCount(std::uint32_t frameIndex) const;

    // Get the name of a scope by index.
    const char* scopeName(std::uint32_t frameIndex, std::uint32_t scopeIndex) const;

    // Get the duration of a scope by index (in nanoseconds).
    // Returns -1.0 if not available.
    double scopeDurationNs(std::uint32_t frameIndex, std::uint32_t scopeIndex) const;

private:
    GpuTimer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    std::uint32_t framesInFlight_ = 2;
    std::uint32_t maxScopesPerFrame_ = 16;

    std::uint32_t timestampValidBits_ = 0;
    double timestampPeriodNs_ = 0.0;

    // Per-frame query pools.
    std::vector<VkQueryPool> queryPools_;

    // Per-frame scope tracking.
    struct ScopeInfo
    {
        const char* name = nullptr;
        std::uint32_t beginQuery = 0;
        std::uint32_t endQuery = 0;
        bool collected = false;
        double durationNs = -1.0;
    };
    std::vector<std::vector<ScopeInfo>> frameScopes_; // [frame][scope]
    std::vector<std::vector<std::uint32_t>> openScopeStacks_; // [frame][open scope indices in LIFO order]

    // Debug label function pointers.
    PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel_ = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT endLabel_ = nullptr;
};

} // namespace starforge::vulkan
