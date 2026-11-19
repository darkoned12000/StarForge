#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace starforge::environment
{

// One UI vertex: clip-space corner (xy, zw unused) + RGBA colour.
struct UiVertex
{
    std::array<float, 4> position{};
    std::array<float, 4> color{};
};
static_assert(sizeof(UiVertex) == 32, "UiVertex must match ui_text.slang");

// Internal UI text renderer. The facade builds clip-space quads from the
// bitmap font; this class uploads them and issues one draw call.
class UiRenderer
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED; // must match the active pass
        std::string shaderDirectory;
    };

    static std::expected<UiRenderer, std::string> create(const CreateInfo& info);
    ~UiRenderer();
    UiRenderer(UiRenderer&&) noexcept;
    UiRenderer& operator=(UiRenderer&&) noexcept;
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    void render(VkCommandBuffer cmd, const std::vector<UiVertex>& vertices) const;

private:
    UiRenderer() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::environment
