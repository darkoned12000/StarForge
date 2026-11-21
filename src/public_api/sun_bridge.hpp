#pragma once

#include "bridge_types.hpp"

#include "world_bridge.hpp"
#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge::public_api_bridge
{

struct RawSunContext
{
    RawMat4f viewProj;
    RawWorldPosition cameraPosition;
    RawVec2f resolution;
    float time = 0.0f;
};

struct RawSunParams
{
    RawWorldPosition worldPosition;
    double radius = 10000.0;
    double coronaRadiusMultiplier = 6.0;
    RawVec4f coreColor;
    RawVec4f limbColor;
    RawVec4f coronaColor;
    float limbDarkeningCoeff = 0.6f;
    float coronaIntensity = 1.0f;
    float time = 0.0f;
};

class RawSun
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory;
        double defaultRadius = 10000.0;
        std::uint32_t subdivisions = 16;
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    [[nodiscard]] static std::expected<std::unique_ptr<RawSun>, std::string>
    create(const CreateInfo& info);

    ~RawSun();
    RawSun(RawSun&& other) noexcept;
    RawSun& operator=(RawSun&& other) noexcept;
    RawSun(const RawSun&) = delete;
    RawSun& operator=(const RawSun&) = delete;

    void render(VkCommandBuffer cmd, const RawSunContext& context, const RawSunParams& params);
    [[nodiscard]] std::expected<void, std::string> recreate(const CreateInfo& info);

private:
    RawSun();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
