#include <doctest/doctest.h>

#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

TEST_CASE("Depth format: findDepthFormat returns a supported format")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        VkFormat depthFormat = findDepthFormat(context->physicalDevice());
        CHECK(depthFormat != VK_FORMAT_UNDEFINED);

        // Verify the format supports depth-stencil attachment.
        CHECK(formatSupportsFeatures(context->physicalDevice(), depthFormat,
                                    VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT));

        // Create a depth image and verify it allocates cleanly.
        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());

        {
            auto depthImage = Image::create(*allocator, {
                .width = 64,
                .height = 64,
                .format = depthFormat,
                .usage = ImageUsageFlags::DepthAttachment,
            });
            REQUIRE(depthImage.has_value());
            CHECK(depthImage->format() == depthFormat);
            CHECK(depthImage->aspectMask() == VK_IMAGE_ASPECT_DEPTH_BIT);

            auto view = depthImage->createView(device->device());
            REQUIRE(view.has_value());
            CHECK(*view != VK_NULL_HANDLE);

            vkDestroyImageView(device->device(), *view, nullptr);
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
