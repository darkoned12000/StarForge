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

// Internal-only bridge for public facades (e.g. Starfield) that need
// backend handles without exposing them in the public interface.
struct RendererInternalAccess
{
    [[nodiscard]] static bool isValid(const Renderer& renderer) noexcept
    {
        return renderer.impl_ && renderer.impl_->device && renderer.impl_->swapchain && renderer.impl_->frameRenderer;
    }

    [[nodiscard]] static VkInstance instance(Renderer& renderer) noexcept
    {
        return renderer.impl_->context->instance();
    }
    [[nodiscard]] static VkDevice device(Renderer& renderer) noexcept
    {
        return renderer.impl_->device->device();
    }
    [[nodiscard]] static VkPhysicalDevice physicalDevice(Renderer& renderer) noexcept
    {
        return renderer.impl_->context->physicalDevice();
    }
    [[nodiscard]] static std::uint32_t queueFamily(Renderer& renderer) noexcept
    {
        return renderer.impl_->device->graphicsQueueFamily();
    }
    [[nodiscard]] static std::uint32_t framesInFlight(Renderer& renderer) noexcept
    {
        return renderer.impl_->frameRenderer->framesInFlight();
    }
    [[nodiscard]] static VkFormat colorFormat(Renderer& renderer) noexcept
    {
        return renderer.impl_->swapchain->format();
    }
    [[nodiscard]] static VkFormat depthFormat(Renderer& renderer) noexcept
    {
        return renderer.impl_->swapchain->depthFormat();
    }
    [[nodiscard]] static VkCommandBuffer commandBuffer(FrameContext& frameContext) noexcept
    {
        return static_cast<VkCommandBuffer>(frameContext.commandBuffer_);
    }
};

} // namespace starforge
