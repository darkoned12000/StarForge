#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

TEST_CASE("VMA memory snapshot: capture before and after allocation")
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

        // Capture before allocation.
        auto before = allocator->captureMemorySnapshot();
        CHECK(before.allocationCount == 0);

        {
            auto buf = Buffer::create(*allocator, {
                .size = 1024,
                .usage = BufferUsage::Storage,
                .memory = MemoryPlacement::GpuPreferred,
            });
            REQUIRE(buf.has_value());

            auto during = allocator->captureMemorySnapshot();
            CHECK(during.allocationCount >= 1);
            CHECK(during.allocatedBytes > 0);
        }

        // After buffer destroyed.
        auto after = allocator->captureMemorySnapshot();
        CHECK(after.allocationCount == 0);

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
