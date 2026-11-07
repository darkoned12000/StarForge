#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

// Internal bridge between the public starfield facade and the environment
// backend. Uses only plain data and Vulkan handles so no translation unit
// ever includes both the public math header and the internal GLM-based math
// aliases (both live in starforge::math). This header must not be installed.
namespace starforge::public_api_bridge
{

struct RawVec3f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RawVec4f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct RawVec2f
{
    float x = 0.0f;
    float y = 0.0f;
};

struct RawMat4f
{
    float columns[4][4] = {};
};

struct RawStarfieldContext
{
    RawMat4f viewProj;
    RawMat4f invViewProj;
    RawVec3f cameraPosition;
    RawVec2f resolution;
    float time = 0.0f;
};

struct RawStarfieldParams
{
    RawVec3f worldPosition;
    float radius = 1.0f;
    float intensity = 1.0f;
    RawVec4f color;
    float limbDarkeningCoeff = 0.3f;
};

// Owns the internal starfield renderer (pipeline, shader modules).
class RawStarfield
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

    [[nodiscard]] static std::expected<std::unique_ptr<RawStarfield>, std::string> create(const CreateInfo& info);

    ~RawStarfield();
    RawStarfield(const RawStarfield&) = delete;
    RawStarfield& operator=(const RawStarfield&) = delete;
    RawStarfield(RawStarfield&& other) noexcept;
    RawStarfield& operator=(RawStarfield&& other) noexcept;

    void render(VkCommandBuffer cmd, const RawStarfieldContext& context, const RawStarfieldParams& params);

private:
    RawStarfield() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
