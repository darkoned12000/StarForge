#pragma once

#include "bridge_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge::public_api_bridge
{

struct RawNebulaContext
{
    RawMat4f viewProj;
    RawMat4f invViewProj;
    RawVec3f cameraPosition;
    RawVec2f resolution;
    float time = 0.0f;
};

struct RawNebulaParams
{
    RawVec4f baseColor;
    float density = 1.0f;
    float absorption = 0.1f;
    float scattering = 0.5f;
    float time = 0.0f;
    bool celShaded = false;
};

class RawNebula
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
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    [[nodiscard]] static std::expected<std::unique_ptr<RawNebula>, std::string> create(const CreateInfo& info);

    ~RawNebula();
    RawNebula(const RawNebula&) = delete;
    RawNebula& operator=(const RawNebula&) = delete;
    RawNebula(RawNebula&& other) noexcept;
    RawNebula& operator=(RawNebula&& other) noexcept;

    void render(VkCommandBuffer cmd, const RawNebulaContext& context, const RawNebulaParams& params);

private:
    RawNebula() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
