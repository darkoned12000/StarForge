#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge
{
class Allocator;
class Buffer;
class Image;
} // namespace starforge

namespace starforge::environment
{

struct HudParams
{
    float panelX = 8.0f, panelY = 8.0f;
    float panelWidth = 112.0f, panelHeight = 48.0f;
    float portraitX = 14.0f, portraitY = 14.0f;
    float portraitWidth = 36.0f, portraitHeight = 36.0f;
    float panelColor[4] = {0.08f, 0.12f, 0.22f, 1.0f};
    float outlineColor[4] = {0.02f, 0.02f, 0.04f, 1.0f};
    float accentColor[4] = {0.95f, 0.65f, 0.15f, 1.0f};
};

// Toon HUD renderer: flat outlined panels, a portrait frame and accent bars.
// Records a fullscreen pass into an active dynamic-rendering pass.
class HudRenderer
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        std::string shaderDirectory;
    };

    static std::expected<HudRenderer, std::string> create(const CreateInfo& info);
    ~HudRenderer();
    HudRenderer(HudRenderer&&) noexcept;
    HudRenderer& operator=(HudRenderer&&) noexcept;
    HudRenderer(const HudRenderer&) = delete;
    HudRenderer& operator=(const HudRenderer&) = delete;

    void render(VkCommandBuffer cmd, float width, float height, const HudParams& params) const;

private:
    HudRenderer() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::environment
