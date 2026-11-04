#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <cstring>

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

TEST_CASE("BDA: buffer device address can be queried and is non-zero")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());

        // Create a buffer with BDA usage.
        auto buffer = Buffer::create(*allocator, {
            .size = 256,
            .usage = BufferUsage::Storage,
            .memory = MemoryPlacement::CpuToGpu,
        });
        REQUIRE(buffer.has_value());

        // Write some data.
        {
            auto mapped = buffer->map();
            REQUIRE(mapped.has_value());
            std::memset(*mapped, 0, 256);
            static_cast<std::uint32_t*>(*mapped)[0] = 42;
            buffer->flush(0, 256);
            buffer->unmap();
        }

        // Query device address.
        VkBufferDeviceAddressInfo addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = buffer->handle();
        VkDeviceAddress addr = vkGetBufferDeviceAddress(device->device(), &addrInfo);

        CHECK(addr != 0);

        CHECK(allocator->allocationCount() == 1);
    }

    CHECK(context->validationErrorCount() == 0);
}
