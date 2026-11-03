#include "vulkan/vulkan_device.hpp"

#include "core/log.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_result.hpp"

#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace starforge::vulkan
{

namespace
{

constexpr const char* kTag = "VULKAN";

void setObjectName(VkInstance instance, VkDevice device, std::uint64_t objectHandle,
                   VkObjectType objectType, const char* name)
{
    if (!instance || !name || !device)
    {
        return;
    }
    const auto setName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
    if (!setName)
    {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = objectType;
    info.objectHandle = objectHandle;
    info.pObjectName = name;
    VkResult result = setName(device, &info);
    (void)result;
}

std::optional<std::uint32_t> findGraphicsQueueFamily(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (std::uint32_t i = 0; i < count; ++i)
    {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
        {
            continue;
        }

        if (surface != VK_NULL_HANDLE)
        {
            VkBool32 presentSupported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupported) != VK_SUCCESS ||
                presentSupported == VK_FALSE)
            {
                continue;
            }
        }

        return i;
    }
    return std::nullopt;
}

bool hasDeviceExtension(VkPhysicalDevice device, const char* name)
{
    std::uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
    {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS &&
        count == 0)
    {
        return false;
    }

    for (const auto& extension : extensions)
    {
        if (std::strcmp(extension.extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

std::expected<VulkanDevice, std::string> VulkanDevice::create(const VulkanContext& context)
{
    return create(context, CreateInfo{});
}

std::expected<VulkanDevice, std::string> VulkanDevice::create(const VulkanContext& context, const CreateInfo& info)
{
    VulkanDevice device;
    const VkPhysicalDevice physicalDevice = context.physicalDevice();

    if (physicalDevice == VK_NULL_HANDLE)
    {
        return std::unexpected("VulkanContext has no selected physical device");
    }

    for (const char* ext : info.requiredExtensions)
    {
        if (!hasDeviceExtension(physicalDevice, ext))
        {
            return std::unexpected(std::string("Required device extension not supported: ") + ext);
        }
    }

    const auto queueFamily = findGraphicsQueueFamily(physicalDevice, info.surface);
    if (!queueFamily)
    {
        return std::unexpected(info.surface != VK_NULL_HANDLE
            ? "No queue family supports both graphics and presentation"
            : "No graphics queue family found");
    }
    device.graphicsQueueFamily_ = *queueFamily;

    const float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = device.graphicsQueueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const GpuCapabilities& caps = context.capabilities();

    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
    meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshFeatures.meshShader = caps.meshShader ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features13;
    features12.bufferDeviceAddress = VK_TRUE;
    if (caps.bindless)
    {
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    }
    if (caps.drawIndirectCount)
    {
        features12.drawIndirectCount = VK_TRUE;
    }

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features12;
    if (caps.multiDrawIndirect)
    {
        features2.features.multiDrawIndirect = VK_TRUE;
    }
    if (caps.shaderFloat64)
    {
        features2.features.shaderFloat64 = VK_TRUE;
    }

    std::vector<const char*> extensions = info.requiredExtensions;
    if (caps.meshShader && hasDeviceExtension(physicalDevice, VK_EXT_MESH_SHADER_EXTENSION_NAME))
    {
        features13.pNext = &meshFeatures;
        bool alreadyListed = false;
        for (const char* ext : extensions)
        {
            if (std::strcmp(ext, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0)
            {
                alreadyListed = true;
                break;
            }
        }
        if (!alreadyListed)
        {
            extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        }
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    core::logInfo(kTag, "Creating logical device ({})", info.surface != VK_NULL_HANDLE ? "with surface" : "headless");
    if (const VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device.device_);
        result != VK_SUCCESS)
    {
        return std::unexpected(describeFailure("vkCreateDevice", result));
    }

    vkGetDeviceQueue(device.device_, device.graphicsQueueFamily_, 0, &device.graphicsQueue_);

    if (device.graphicsQueue_ == VK_NULL_HANDLE)
    {
        return std::unexpected("Failed to retrieve graphics queue");
    }

    setObjectName(context.instance(), device.device_,
                  reinterpret_cast<std::uintptr_t>(device.device_),
                  VK_OBJECT_TYPE_DEVICE, "STARFORGE_Device");
    setObjectName(context.instance(), device.device_,
                  reinterpret_cast<std::uintptr_t>(device.graphicsQueue_),
                  VK_OBJECT_TYPE_QUEUE, "STARFORGE_GraphicsQueue");

    return device;
}

VulkanDevice::~VulkanDevice()
{
    destroy();
}

VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      graphicsQueue_(std::exchange(other.graphicsQueue_, VK_NULL_HANDLE)),
      graphicsQueueFamily_(other.graphicsQueueFamily_)
{
}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        graphicsQueue_ = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
        graphicsQueueFamily_ = other.graphicsQueueFamily_;
    }
    return *this;
}

void VulkanDevice::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    graphicsQueue_ = VK_NULL_HANDLE;
    graphicsQueueFamily_ = 0;
}

} // namespace starforge::vulkan
