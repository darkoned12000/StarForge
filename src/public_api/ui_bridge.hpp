#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <expected>
#include <memory>
#include <string>

namespace starforge::public_api_bridge
{

struct RawUiTextDraw
{
    float x = 0.0f;
    float y = 0.0f;
    float scale = 2.0f;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::string text;
};

class RawUiCanvas
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory;
    };

    static std::expected<std::unique_ptr<RawUiCanvas>, std::string> create(const CreateInfo& info);
    ~RawUiCanvas();
    RawUiCanvas(RawUiCanvas&&) noexcept;
    RawUiCanvas& operator=(RawUiCanvas&&) noexcept;
    RawUiCanvas(const RawUiCanvas&) = delete;
    RawUiCanvas& operator=(const RawUiCanvas&) = delete;

    void clear() noexcept;
    void addText(const RawUiTextDraw& draw);
    void render(VkCommandBuffer cmd, float width, float height);
    [[nodiscard]] std::size_t vertexCount() const noexcept;

private:
    RawUiCanvas() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
