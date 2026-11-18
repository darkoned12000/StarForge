#pragma once

#include "bridge_types.hpp"

#include <vulkan/vulkan.h>

#include <expected>
#include <memory>
#include <string>

namespace starforge::public_api_bridge
{

struct RawHudParams
{
    float panelX = 8.0f, panelY = 8.0f, panelWidth = 112.0f, panelHeight = 48.0f;
    float portraitX = 14.0f, portraitY = 14.0f, portraitWidth = 36.0f, portraitHeight = 36.0f;
    float panelColor[4] = {0.08f, 0.12f, 0.22f, 1.0f};
    float outlineColor[4] = {0.02f, 0.02f, 0.04f, 1.0f};
    float accentColor[4] = {0.95f, 0.65f, 0.15f, 1.0f};
};

class RawHud
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory;
    };

    static std::expected<std::unique_ptr<RawHud>, std::string> create(const CreateInfo& info);
    ~RawHud();
    RawHud(RawHud&&) noexcept;
    RawHud& operator=(RawHud&&) noexcept;
    RawHud(const RawHud&) = delete;
    RawHud& operator=(const RawHud&) = delete;

    void render(VkCommandBuffer cmd, float width, float height, const RawHudParams& params);

private:
    RawHud() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
