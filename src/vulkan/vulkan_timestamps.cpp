#include "vulkan/vulkan_timestamps.hpp"

#include "core/log.hpp"
#include "vulkan/vulkan_result.hpp"

#include <cstring>
#include <utility>
#include <vector>

namespace starforge::vulkan
{

namespace
{
constexpr const char* kTag = "VULKAN";
}

std::expected<GpuTimer, std::string> GpuTimer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.physicalDevice == VK_NULL_HANDLE)
    {
        return std::unexpected("GpuTimer requires device and physicalDevice");
    }

    GpuTimer timer;
    timer.device_ = info.device;
    timer.queueFamily_ = info.queueFamily;
    timer.framesInFlight_ = std::max(1u, info.framesInFlight);
    timer.maxScopesPerFrame_ = std::max(1u, info.maxScopesPerFrame);

    // Query timestamp support.
    VkQueueFamilyProperties2 queueProps{};
    queueProps.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    VkQueueFamilyProperties& props = queueProps.queueFamilyProperties;

    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(info.physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties2> queueFamilies(queueFamilyCount);
    for (auto& qf : queueFamilies)
    {
        qf.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(info.physicalDevice, &queueFamilyCount, queueFamilies.data());

    if (info.queueFamily < queueFamilyCount)
    {
        timer.timestampValidBits_ = queueFamilies[info.queueFamily].queueFamilyProperties.timestampValidBits;
        // timestampPeriod is in nanoseconds per tick.
        VkPhysicalDeviceProperties2 physProps{};
        physProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(info.physicalDevice, &physProps);
        timer.timestampPeriodNs_ = static_cast<double>(physProps.properties.limits.timestampPeriod);
    }

    if (timer.timestampValidBits_ == 0)
    {
        core::logWarning(kTag, "GPU timestamps not supported on queue family %u", info.queueFamily);
    }
    else
    {
        core::logInfo(kTag, "GPU timestamps: %u valid bits, period %.4f ns", timer.timestampValidBits_, timer.timestampPeriodNs_);
    }

    // Create per-frame query pools.
    // Each frame needs 2 queries per scope (begin + end).
    const std::uint32_t queriesPerFrame = timer.maxScopesPerFrame_ * 2;

    timer.queryPools_.resize(timer.framesInFlight_);
    for (std::uint32_t i = 0; i < timer.framesInFlight_; ++i)
    {
        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = queriesPerFrame;

        if (const VkResult result = vkCreateQueryPool(info.device, &poolInfo, nullptr, &timer.queryPools_[i]);
            result != VK_SUCCESS)
        {
            // Clean up already created pools.
            for (std::uint32_t j = 0; j < i; ++j)
            {
                vkDestroyQueryPool(info.device, timer.queryPools_[j], nullptr);
            }
            return std::unexpected(describeFailure("vkCreateQueryPool", result));
        }
    }

    timer.frameScopes_.resize(timer.framesInFlight_);
    timer.openScopeStacks_.resize(timer.framesInFlight_);

    // Get debug label function pointers (optional).
    timer.beginLabel_ = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(info.device, "vkCmdBeginDebugUtilsLabelEXT"));
    timer.endLabel_ = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(info.device, "vkCmdEndDebugUtilsLabelEXT"));

    return timer;
}

GpuTimer::~GpuTimer()
{
    destroy();
}

GpuTimer::GpuTimer(GpuTimer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      queueFamily_(other.queueFamily_),
      framesInFlight_(other.framesInFlight_),
      maxScopesPerFrame_(other.maxScopesPerFrame_),
      timestampValidBits_(other.timestampValidBits_),
      timestampPeriodNs_(other.timestampPeriodNs_),
      queryPools_(std::move(other.queryPools_)),
      frameScopes_(std::move(other.frameScopes_)),
      openScopeStacks_(std::move(other.openScopeStacks_)),
      beginLabel_(other.beginLabel_),
      endLabel_(other.endLabel_)
{
}

GpuTimer& GpuTimer::operator=(GpuTimer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        queueFamily_ = other.queueFamily_;
        framesInFlight_ = other.framesInFlight_;
        maxScopesPerFrame_ = other.maxScopesPerFrame_;
        timestampValidBits_ = other.timestampValidBits_;
        timestampPeriodNs_ = other.timestampPeriodNs_;
        queryPools_ = std::move(other.queryPools_);
        frameScopes_ = std::move(other.frameScopes_);
        openScopeStacks_ = std::move(other.openScopeStacks_);
        beginLabel_ = other.beginLabel_;
        endLabel_ = other.endLabel_;
    }
    return *this;
}

void GpuTimer::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        for (auto pool : queryPools_)
        {
            if (pool != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(device_, pool, nullptr);
            }
        }
    }
    device_ = VK_NULL_HANDLE;
    queryPools_.clear();
    frameScopes_.clear();
    openScopeStacks_.clear();
}

VkQueryPool GpuTimer::queryPool(std::uint32_t frameIndex) const
{
    if (frameIndex < queryPools_.size())
    {
        return queryPools_[frameIndex];
    }
    return VK_NULL_HANDLE;
}

void GpuTimer::resetQueryPool(VkCommandBuffer cmd, std::uint32_t frameIndex, std::uint32_t queryCount)
{
    if (frameIndex < queryPools_.size() && queryPools_[frameIndex] != VK_NULL_HANDLE)
    {
        vkCmdResetQueryPool(cmd, queryPools_[frameIndex], 0, queryCount);
    }
    if (frameIndex < frameScopes_.size())
    {
        frameScopes_[frameIndex].clear();
    }
    if (frameIndex < openScopeStacks_.size())
    {
        openScopeStacks_[frameIndex].clear();
    }
}

void GpuTimer::writeTimestamp(VkCommandBuffer cmd, std::uint32_t frameIndex,
                              std::uint32_t queryIndex, VkPipelineStageFlags2 stage)
{
    if (frameIndex >= queryPools_.size() || queryPools_[frameIndex] == VK_NULL_HANDLE)
    {
        return;
    }
    vkCmdWriteTimestamp2(cmd, stage, queryPools_[frameIndex], queryIndex);
}

void GpuTimer::pushLabel(VkCommandBuffer cmd, const char* name)
{
    if (beginLabel_ && name)
    {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        beginLabel_(cmd, &label);
    }
}

void GpuTimer::popLabel(VkCommandBuffer cmd)
{
    if (endLabel_)
    {
        endLabel_(cmd);
    }
}

std::uint32_t GpuTimer::beginScope(VkCommandBuffer cmd, std::uint32_t frameIndex, const char* name)
{
    if (frameIndex >= frameScopes_.size() || frameIndex >= openScopeStacks_.size())
    {
        return 0;
    }

    if (frameScopes_[frameIndex].size() >= maxScopesPerFrame_)
    {
        return UINT32_MAX;
    }

    const std::uint32_t scopeIndex = static_cast<std::uint32_t>(frameScopes_[frameIndex].size());
    const std::uint32_t beginQuery = scopeIndex * 2;
    const std::uint32_t endQuery = beginQuery + 1;

    ScopeInfo info;
    info.name = name;
    info.beginQuery = beginQuery;
    info.endQuery = endQuery;
    info.collected = false;
    info.durationNs = -1.0;
    frameScopes_[frameIndex].push_back(info);

    openScopeStacks_[frameIndex].push_back(scopeIndex);

    pushLabel(cmd, name);
    writeTimestamp(cmd, frameIndex, beginQuery, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);

    return beginQuery;
}

std::uint32_t GpuTimer::endScope(VkCommandBuffer cmd, std::uint32_t frameIndex)
{
    if (frameIndex >= frameScopes_.size() || frameScopes_[frameIndex].empty() ||
        openScopeStacks_[frameIndex].empty())
    {
        return 0;
    }

    const std::uint32_t scopeIndex = openScopeStacks_[frameIndex].back();
    openScopeStacks_[frameIndex].pop_back();

    if (scopeIndex >= frameScopes_[frameIndex].size())
    {
        return 0;
    }

    auto& scope = frameScopes_[frameIndex][scopeIndex];
    writeTimestamp(cmd, frameIndex, scope.endQuery, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    popLabel(cmd);

    return scope.endQuery;
}

bool GpuTimer::collectResults(std::uint32_t frameIndex)
{
    if (frameIndex >= queryPools_.size() || frameIndex >= frameScopes_.size())
    {
        return false;
    }

    VkQueryPool pool = queryPools_[frameIndex];
    if (pool == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::uint32_t queryCount = static_cast<std::uint32_t>(frameScopes_[frameIndex].size()) * 2;
    if (queryCount == 0)
    {
        return true;
    }

    // Check availability first (non-blocking). Use stride of 16 bytes
    // (8 for result + 8 for availability) as required by the spec.
    std::vector<std::uint64_t> results(queryCount * 2);
    VkResult availResult = vkGetQueryPoolResults(
        device_, pool, 0, queryCount, queryCount * 2 * sizeof(std::uint64_t),
        results.data(), 2 * sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    if (availResult != VK_SUCCESS)
    {
        return false;
    }

    // Results are in the even-indexed slots (stride = 16 bytes: 8 for result + 8 for availability).
    // Handle timestamp wraparound using modular arithmetic. Mask both timestamps
    // to validBits, then compute (end - begin) & mask, which naturally handles
    // wraparound for both 64-bit and sub-64-bit clocks.
    const std::uint64_t mask = (timestampValidBits_ >= 64)
        ? ~0ULL
        : ((1ULL << timestampValidBits_) - 1ULL);

    for (auto& scope : frameScopes_[frameIndex])
    {
        const std::uint64_t beginTs = results[scope.beginQuery * 2] & mask;
        const std::uint64_t endTs = results[scope.endQuery * 2] & mask;
        const std::uint64_t delta = (endTs - beginTs) & mask;

        scope.durationNs = static_cast<double>(delta) * timestampPeriodNs_;
        scope.collected = true;
    }

    return true;
}

double GpuTimer::durationNs(std::uint32_t frameIndex, std::uint32_t beginQuery,
                            std::uint32_t endQuery) const
{
    if (frameIndex >= frameScopes_.size())
    {
        return -1.0;
    }

    // Find the scope matching these query indices.
    for (const auto& scope : frameScopes_[frameIndex])
    {
        if (scope.beginQuery == beginQuery && scope.endQuery == endQuery)
        {
            return scope.durationNs;
        }
    }
    return -1.0;
}

std::uint32_t GpuTimer::scopeCount(std::uint32_t frameIndex) const
{
    if (frameIndex < frameScopes_.size())
    {
        return static_cast<std::uint32_t>(frameScopes_[frameIndex].size());
    }
    return 0;
}

const char* GpuTimer::scopeName(std::uint32_t frameIndex, std::uint32_t scopeIndex) const
{
    if (frameIndex < frameScopes_.size() && scopeIndex < frameScopes_[frameIndex].size())
    {
        return frameScopes_[frameIndex][scopeIndex].name;
    }
    return nullptr;
}

double GpuTimer::scopeDurationNs(std::uint32_t frameIndex, std::uint32_t scopeIndex) const
{
    if (frameIndex < frameScopes_.size() && scopeIndex < frameScopes_[frameIndex].size())
    {
        return frameScopes_[frameIndex][scopeIndex].durationNs;
    }
    return -1.0;
}

} // namespace starforge::vulkan
