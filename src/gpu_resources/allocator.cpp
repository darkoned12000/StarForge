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
    : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE))
{
}

Allocator& Allocator::operator=(Allocator&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
    }
    return *this;
}

void Allocator::destroy() noexcept
{
    if (allocator_ != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
}

std::uint32_t Allocator::allocationCount() const noexcept
{
    if (allocator_ == VK_NULL_HANDLE)
    {
        return 0;
    }
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(allocator_, &stats);
    return stats.total.statistics.allocationCount;
}

} // namespace starforge::gpu_resources
