#pragma once

#include "vulkan/vulkan_capabilities.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::vulkan
{

// Validation defaults to on only when CMake enabled it for a Debug build.
#ifdef STARFORGE_ENABLE_VALIDATION
inline constexpr bool kValidationDefault = true;
#else
inline constexpr bool kValidationDefault = false;
#endif

struct ContextCreateInfo
{
    std::string applicationName = "STARFORGE";
    std::uint32_t applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);

    // Instance extensions required by the windowing system.
    std::vector<const char*> requiredExtensions;

    // Requests VK_LAYER_KHRONOS_validation + a debug messenger. Falls back to a
    // warning (not an error) when the layer is not installed.
    bool enableValidation = kValidationDefault;

    // Optional features to treat as unavailable (comma-separated names, see
    // optionalFeatures()). Defaults to the STARFORGE_DISABLE_FEATURES
    // environment variable so fallback paths can be forced on capable GPUs.
    std::string disabledFeatures = disabledFeaturesFromEnvironment();
};

// Owns the VkInstance, the optional debug messenger and the selected physical
// device. Move-only; resources are released in the destructor.
class VulkanContext
{
public:
    [[nodiscard]] static std::expected<VulkanContext, std::string>
    create(const ContextCreateInfo& info);

    ~VulkanContext();
    VulkanContext(VulkanContext&& other) noexcept;
    VulkanContext& operator=(VulkanContext&& other) noexcept;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] const GpuCapabilities& capabilities() const noexcept { return capabilities_; }
    [[nodiscard]] std::uint32_t instanceApiVersion() const noexcept { return instanceApiVersion_; }
    [[nodiscard]] bool validationEnabled() const noexcept { return messenger_ != VK_NULL_HANDLE; }
    [[nodiscard]] std::uint64_t validationErrorCount() const noexcept { return validationErrorCount_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t validationWarningCount() const noexcept { return validationWarningCount_.load(std::memory_order_relaxed); }

    // For debug messenger pUserData access.
    [[nodiscard]] std::atomic<std::uint64_t>& errorCounter() noexcept { return validationErrorCount_; }
    [[nodiscard]] std::atomic<std::uint64_t>& warningCounter() noexcept { return validationWarningCount_; }

private:
    VulkanContext() = default;
    void destroy() noexcept;

    VkInstance                 instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT   messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice           physicalDevice_ = VK_NULL_HANDLE;
    GpuCapabilities            capabilities_;
    std::uint32_t              instanceApiVersion_ = 0;
    std::atomic<std::uint64_t> validationErrorCount_{0};
    std::atomic<std::uint64_t> validationWarningCount_{0};
};

} // namespace starforge::vulkan
