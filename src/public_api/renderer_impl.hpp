#pragma once

#include "starforge/renderer.hpp"

#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_renderer.hpp"
#include "vulkan/vulkan_swapchain.hpp"

#include <vulkan/vulkan.h>

#include <memory>

namespace starforge
{

// Internal implementation of the public renderer owner. Destruction order is
// explicit: frame renderer, swapchain, device, surface, then context.
struct Renderer::Impl
{
    Application* app = nullptr;

    std::unique_ptr<vulkan::VulkanContext> context;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::unique_ptr<vulkan::VulkanDevice> device;
    std::unique_ptr<vulkan::VulkanSwapchain> swapchain;
    std::unique_ptr<vulkan::FrameRenderer> frameRenderer;

    void waitIdle() const noexcept
    {
        if (device)
        {
            vkDeviceWaitIdle(device->device());
        }
    }

    ~Impl()
    {
        waitIdle();
        frameRenderer.reset();
        swapchain.reset();
        device.reset();
        if (surface != VK_NULL_HANDLE && context)
        {
            vkDestroySurfaceKHR(context->instance(), surface, nullptr);
        }
        surface = VK_NULL_HANDLE;
        context.reset();
    }
};

} // namespace starforge
