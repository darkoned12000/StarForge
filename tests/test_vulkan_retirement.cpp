#include <doctest/doctest.h>

#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_retirement.hpp"

using namespace starforge::vulkan;

TEST_CASE("GPU retirement queue: deleters run only after fence signal")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence pendingFence = VK_NULL_HANDLE;
        VkFence signaledFence = VK_NULL_HANDLE;
        REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &pendingFence) == VK_SUCCESS);
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        REQUIRE(vkCreateFence(device->device(), &fenceInfo, nullptr, &signaledFence) == VK_SUCCESS);

        GpuRetirementQueue queue;
        bool pendingDeleted = false;
        bool signaledDeleted = false;

        queue.retire(pendingFence, [&] { pendingDeleted = true; });
        queue.retire(signaledFence, [&] { signaledDeleted = true; });
        CHECK(queue.pending() == 2);

        // Only the signaled fence's resource is collected.
        CHECK(queue.collect(device->device()) == 1);
        CHECK(signaledDeleted);
        CHECK(!pendingDeleted);
        CHECK(queue.pending() == 1);

        // Signal the pending fence (empty submit), then collect again.
        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        REQUIRE(vkQueueSubmit2(device->graphicsQueue(), 1, &submitInfo, pendingFence) == VK_SUCCESS);
        REQUIRE(vkWaitForFences(device->device(), 1, &pendingFence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

        CHECK(queue.collect(device->device()) == 1);
        CHECK(pendingDeleted);
        CHECK(queue.pending() == 0);

        vkDestroyFence(device->device(), pendingFence, nullptr);
        vkDestroyFence(device->device(), signaledFence, nullptr);
    }

    CHECK(context->validationErrorCount() == 0);
}
