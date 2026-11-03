#include <doctest/doctest.h>

#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

using namespace starforge::vulkan;

TEST_CASE("headless VulkanDevice create/destroy with no window")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        VulkanDevice::CreateInfo deviceInfo;
        auto device = VulkanDevice::create(*context, deviceInfo);
        REQUIRE(device.has_value());
        CHECK(device->device() != VK_NULL_HANDLE);
        CHECK(device->graphicsQueue() != VK_NULL_HANDLE);
    }

    // Asserted after device destruction so teardown validation errors are caught.
    CHECK(context->validationErrorCount() == 0);
}
