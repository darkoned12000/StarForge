#pragma once

#include "starforge/renderer.hpp"

#include "environment/internal/texture_effect.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_renderer.hpp"
#include "vulkan/vulkan_shader_specialization.hpp"
#include "vulkan/vulkan_swapchain.hpp"

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

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

    // Offscreen HDR scene target, per frame-in-flight to avoid GPU hazards
    // when multiple frames are in flight. Scene shaders write linear RGBA16F,
    // then a fullscreen tonemap composites into the 8-bit swapchain.
    // STARFORGE_HDR=0 falls back to the direct path.
    bool hdrEnabled = true;
    float hdrExposure = 1.0f;
    std::uint32_t hdrFrameCounter = 0;
    VkExtent2D hdrExtent{};
    std::unique_ptr<gpu_resources::Allocator> allocator;

    struct HdrFrameResources
    {
        std::unique_ptr<gpu_resources::Image> color;
        std::unique_ptr<gpu_resources::Image::ImageView> colorView;
        std::unique_ptr<gpu_resources::Image> depth;
        std::unique_ptr<gpu_resources::Image::ImageView> depthView;
        std::unique_ptr<gpu_resources::Image> bloomA;
        std::unique_ptr<gpu_resources::Image::ImageView> bloomAView;
        std::unique_ptr<gpu_resources::Image> bloomB;
        std::unique_ptr<gpu_resources::Image::ImageView> bloomBView;
    };
    std::vector<HdrFrameResources> hdrFrames;
    std::vector<VkImage> hdrColorImages;
    std::vector<VkImageView> hdrColorViews;
    std::vector<VkImage> hdrDepthImages;
    std::vector<VkImageView> hdrDepthViews;

    std::unique_ptr<environment::internal::TextureEffect> brightPass;
    std::unique_ptr<environment::internal::TextureEffect> blurPass;
    std::unique_ptr<environment::internal::TextureEffect> tonemap;

    void waitIdle() const noexcept
    {
        if (device)
        {
            vkDeviceWaitIdle(device->device());
        }
    }

    void destroyHdrResources() noexcept
    {
        tonemap.reset();
        blurPass.reset();
        brightPass.reset();
        hdrColorImages.clear();
        hdrColorViews.clear();
        hdrDepthImages.clear();
        hdrDepthViews.clear();
        hdrFrames.clear();
        allocator.reset();
        hdrExtent = {};
    }

    ~Impl()
    {
        waitIdle();
        destroyHdrResources();
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
    // Format used for UI/HUD pipelines, which always go straight to the
    // 8-bit swapchain (after the tonemap pass when HDR is enabled).
    [[nodiscard]] static VkFormat colorFormat(Renderer& renderer) noexcept
    {
        return renderer.impl_->swapchain->format();
    }
    [[nodiscard]] static VkFormat uiColorFormat(Renderer& renderer) noexcept
    {
        return colorFormat(renderer);
    }
    // Format used for scene pipelines (toon, starfield, nebula, planet,
    // rocks, sun). These target the linear HDR buffer when enabled.
    [[nodiscard]] static VkFormat sceneColorFormat(Renderer& renderer) noexcept
    {
        return renderer.impl_->hdrEnabled ? VK_FORMAT_R16G16B16A16_SFLOAT
                                          : renderer.impl_->swapchain->format();
    }
    // Shader specialization constant 0 (kHdrOutput) is true only for the
    // HDR scene path; LDR fallbacks keep the in-shader shoulder.
    [[nodiscard]] static const VkSpecializationInfo* specializationInfo(Renderer& renderer) noexcept
    {
        return &vulkan::hdrOutputSpecialization(renderer.impl_->hdrEnabled);
    }
    [[nodiscard]] static VkFormat depthFormat(Renderer& renderer) noexcept
    {
        return renderer.impl_->swapchain->depthFormat();
    }
    [[nodiscard]] static VkCommandBuffer commandBuffer(FrameContext& frameContext) noexcept
    {
        return static_cast<VkCommandBuffer>(frameContext.commandBuffer_);
    }

    // Offscreen HDR target lifecycle (defined in renderer.cpp).
    [[nodiscard]] static bool hdrRequested() noexcept;
    [[nodiscard]] static std::expected<void, std::string> createHdrResources(Renderer::Impl& impl);
    static void destroyHdrResources(Renderer::Impl& impl) noexcept;
};

} // namespace starforge
