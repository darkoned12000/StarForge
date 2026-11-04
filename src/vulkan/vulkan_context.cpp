#include "vulkan/vulkan_context.hpp"

#include "core/log.hpp"
#include "vulkan/vulkan_physical_device.hpp"
#include "vulkan/vulkan_result.hpp"
#include "vulkan/vulkan_version.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>
#include <vector>

namespace starforge::vulkan
{

namespace
{

constexpr const char* kTag = "VULKAN";
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

// (global pointers removed — context passed via pUserData)

bool hasInstanceLayer(const char* name)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
    {
        return false;
    }

    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) < VK_SUCCESS)
    {
        return false;
    }

    return std::ranges::any_of(layers, [name](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

bool hasInstanceExtension(const char* name)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
    {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) < VK_SUCCESS)
    {
        return false;
    }

    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData)
{
    const char* id = data->pMessageIdName ? data->pMessageIdName : "";

    auto* context = static_cast<VulkanContext*>(userData);

    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        if (context)
        {
            context->errorCounter().fetch_add(1, std::memory_order_relaxed);
        }
        core::logError("VALIDATION", "{}: {}", id, data->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        if (context)
        {
            context->warningCounter().fetch_add(1, std::memory_order_relaxed);
        }
        core::logWarning("VALIDATION", "{}: {}", id, data->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        core::logInfo("VALIDATION", "{}: {}", id, data->pMessage);
    }
    else
    {
        core::logDebug("VALIDATION", "{}: {}", id, data->pMessage);
    }

    return VK_FALSE; // never abort the call that triggered the message
}

VkDebugUtilsMessengerCreateInfoEXT makeMessengerInfo(void* userData)
{
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    info.pUserData = userData;
    return info;
}

} // namespace

std::expected<VulkanContext, std::string> VulkanContext::create(const ContextCreateInfo& info)
{
    // The object exists from the first step, so every early return below is
    // cleaned up by its destructor.
    VulkanContext context;

    // ---- Loader version ----------------------------------------------------

    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (const VkResult result = vkEnumerateInstanceVersion(&loaderVersion); result != VK_SUCCESS)
    {
        return std::unexpected(describeFailure("vkEnumerateInstanceVersion", result));
    }

    core::logInfo(kTag, "Loader version: {}", apiVersionString(loaderVersion));

    if (!meetsMinimumApiVersion(loaderVersion))
    {
        return std::unexpected(std::format("Vulkan {} or newer is required, loader reports {}",
            apiVersionString(kMinimumApiVersion), apiVersionString(loaderVersion)));
    }

    // Ask for 1.4 when available, otherwise fall back to the 1.3 floor.
    context.instanceApiVersion_ = std::min(loaderVersion, kPreferredApiVersion);

    // ---- Layers and extensions --------------------------------------------

    std::vector<const char*> extensions = info.requiredExtensions;
    std::vector<const char*> layers;
    bool wantMessenger = false;

    if (info.enableValidation)
    {
        if (!hasInstanceLayer(kValidationLayer))
        {
            core::logWarning(kTag, "Validation requested but {} is not installed", kValidationLayer);
        }
        else
        {
            layers.push_back(kValidationLayer);

            if (hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                wantMessenger = true;
            }
            else
            {
                core::logWarning(kTag, "{} unavailable; validation output will not be routed",
                    VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
        }
    }

    // ---- Instance ----------------------------------------------------------

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = info.applicationName.c_str();
    applicationInfo.applicationVersion = info.applicationVersion;
    applicationInfo.pEngineName = "STARFORGE";
    applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.apiVersion = context.instanceApiVersion_;

    // Chained so messages emitted during vkCreate/DestroyInstance are captured.
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo = makeMessengerInfo(&context);

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = wantMessenger ? &messengerInfo : nullptr;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();

    core::logInfo(kTag, "Creating instance (API {}, validation {})",
        apiVersionString(context.instanceApiVersion_), wantMessenger ? "on" : "off");

    if (const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &context.instance_);
        result != VK_SUCCESS)
    {
        return std::unexpected(describeFailure("vkCreateInstance", result));
    }

    // ---- Debug messenger ---------------------------------------------------

    if (wantMessenger)
    {
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(context.instance_, "vkCreateDebugUtilsMessengerEXT"));

        if (!createMessenger ||
            createMessenger(context.instance_, &messengerInfo, nullptr, &context.messenger_) !=
                VK_SUCCESS)
        {
            core::logWarning(kTag, "Failed to create debug messenger; continuing without it");
            context.messenger_ = VK_NULL_HANDLE;
        }
        else
        {
            // Counters accessed via pUserData in debugCallback.
        }
    }

    // ---- Physical device ---------------------------------------------------

    auto selection = selectPhysicalDevice(context.instance_, info.disabledFeatures);
    if (!selection)
    {
        return std::unexpected(std::move(selection.error()));
    }

    context.physicalDevice_ = selection->physicalDevice;
    context.capabilities_ = std::move(selection->capabilities);

    return context;
}

VulkanContext::~VulkanContext()
{
    destroy();
}

VulkanContext::VulkanContext(VulkanContext&& other) noexcept
    : instance_(std::exchange(other.instance_, VK_NULL_HANDLE)),
      messenger_(std::exchange(other.messenger_, VK_NULL_HANDLE)),
      physicalDevice_(std::exchange(other.physicalDevice_, VK_NULL_HANDLE)),
      capabilities_(std::move(other.capabilities_)),
      instanceApiVersion_(other.instanceApiVersion_),
      validationErrorCount_(other.validationErrorCount_.load(std::memory_order_relaxed)),
      validationWarningCount_(other.validationWarningCount_.load(std::memory_order_relaxed))
{
    // Counters accessed via pUserData; no global pointer update needed.
}

VulkanContext& VulkanContext::operator=(VulkanContext&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
        messenger_ = std::exchange(other.messenger_, VK_NULL_HANDLE);
        physicalDevice_ = std::exchange(other.physicalDevice_, VK_NULL_HANDLE);
        capabilities_ = std::move(other.capabilities_);
        instanceApiVersion_ = other.instanceApiVersion_;
        validationErrorCount_.store(other.validationErrorCount_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        validationWarningCount_.store(other.validationWarningCount_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        // Counters accessed via pUserData; no global pointer update needed.
    }
    return *this;
}

void VulkanContext::destroy() noexcept
{
    if (messenger_ != VK_NULL_HANDLE)
    {
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));

        if (destroyMessenger)
        {
            destroyMessenger(instance_, messenger_, nullptr);
        }
        messenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    physicalDevice_ = VK_NULL_HANDLE;
}

} // namespace starforge::vulkan
