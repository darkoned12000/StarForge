#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

// Internal bridge between the public toon facade and the environment
// backend. Uses only plain data and Vulkan handles so no translation unit
// ever includes both the public math header and the internal GLM-based
// math aliases (both live in starforge::math). This header must not be
// installed.
namespace starforge::public_api_bridge
{

struct RawVec3f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RawVec2f
{
    float x = 0.0f;
    float y = 0.0f;
};

struct RawVec4f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct RawMat4f
{
    float columns[4][4] = {};
};

struct RawToonContext
{
    RawMat4f viewProj;
    RawMat4f modelView;
    RawVec3f lightDirectionView;
    RawVec2f resolution;
};

struct RawToonParams
{
    RawVec4f baseColor;
    RawVec4f shadowColor;
    float bandCount = 3.0f;
    float rimStrength = 0.5f;
    float outlineWidth = 2.0f;
};

// Owns the internal toon renderer (pipelines, shader modules).
class RawToon
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory;
    };

    [[nodiscard]] static std::expected<std::unique_ptr<RawToon>, std::string> create(const CreateInfo& info);

    ~RawToon();
    RawToon(const RawToon&) = delete;
    RawToon& operator=(const RawToon&) = delete;
    RawToon(RawToon&& other) noexcept;
    RawToon& operator=(RawToon&& other) noexcept;

    void render(VkCommandBuffer cmd, const RawToonContext& context, const RawToonParams& params);

private:
    RawToon() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
