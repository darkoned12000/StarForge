#include "gpu_resources/allocator.hpp"

#include "core/log.hpp"
#include "vulkan/vulkan_result.hpp"

#include <utility>

namespace starforge::gpu_resources
{

namespace
{
constexpr const char* kTag = "GPU";
}

std::expected<Allocator, std::string> Allocator::create(const CreateInfo& info)
{
    if (info.instance == VK_NULL_HANDLE || info.physicalDevice == VK_NULL_HANDLE || info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("Allocator::create requires instance, physicalDevice and device");
    }

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.instance = info.instance;
    allocatorInfo.physicalDevice = info.physicalDevice;
    allocatorInfo.device = info.device;

    Allocator allocator;
    const VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator.allocator_);
    if (result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vmaCreateAllocator", result));
    }

    core::logInfo(kTag, "VMA allocator created");
    return allocator;
}

Allocator::~Allocator()
{
    destroy();
}

Allocator::Allocator(Allocator&& other) noexcept
    : allocator_(std::exchange(other.allocator_, nullptr))
{
}

Allocator& Allocator::operator=(Allocator&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        allocator_ = std::exchange(other.allocator_, nullptr);
    }
    return *this;
}

void Allocator::destroy() noexcept
{
    if (allocator_ != nullptr)
    {
#ifndef NDEBUG
        if (allocationCount() != 0)
        {
            // Leak: resources not destroyed before allocator.
        }
#endif
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
}

std::uint32_t Allocator::allocationCount() const noexcept
{
    if (allocator_ == nullptr)
    {
        return 0;
    }
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(allocator_, &stats);
    return stats.total.statistics.allocationCount;
}

Allocator::MemorySnapshot Allocator::captureMemorySnapshot() const noexcept
{
    MemorySnapshot snapshot{};
    if (allocator_ == nullptr)
    {
        return snapshot;
    }

    VmaTotalStatistics stats{};
    vmaCalculateStatistics(allocator_, &stats);

    // blockBytes: total device memory allocated from Vulkan (allocatedBytes).
    // allocationBytes: total bytes consumed by active suballocations (usedBytes).
    snapshot.allocatedBytes = stats.total.statistics.blockBytes;
    snapshot.usedBytes = stats.total.statistics.allocationBytes;
    snapshot.allocationCount = stats.total.statistics.allocationCount;
    snapshot.blockCount = stats.total.statistics.blockCount;

    // Device-local heap usage and budget (VK_EXT_memory_budget).
    const VkPhysicalDeviceMemoryProperties* memProps = nullptr;
    vmaGetMemoryProperties(allocator_, &memProps);
    std::uint32_t heapCount = memProps->memoryHeapCount;
    if (heapCount > VK_MAX_MEMORY_HEAPS)
    {
        heapCount = VK_MAX_MEMORY_HEAPS;
    }
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(allocator_, budgets);
    for (std::uint32_t i = 0; i < heapCount; ++i)
    {
        if (memProps->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            snapshot.deviceLocalUsage += budgets[i].usage;
            snapshot.deviceLocalBudget += budgets[i].budget;
        }
    }

    return snapshot;
}

} // namespace starforge::gpu_resources
