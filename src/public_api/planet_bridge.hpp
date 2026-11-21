#pragma once

#include "bridge_types.hpp"

#include <vulkan/vulkan.h>

#include <expected>
#include <memory>
#include <string>

namespace starforge::public_api_bridge
{

struct RawPlanetContext
{
    RawMat4f viewProj{};
    RawMat4f invViewProj{};
    RawVec3f cameraPosition{};
    RawVec2f resolution{};
    float time = 0.0f;
};

struct RawPlanetParams
{
    RawVec3f center{};
    float radius = 6000.0f;
    RawVec4f baseColor{};
    RawVec4f shadowColor{};
    RawVec4f rimColor{};
    RawVec3f sunDirection{};
    float fresnelPower = 3.0f;
    float rimStrength = 0.8f;
    float atmosphereRatio = 1.15f;
    float atmosphereFalloff = 2.0f;
    RawVec4f atmosphereColor{};
    float atmosphereIntensity = 1.0f;
    float depthBias = 0.0005f;
    float spin = 0.0f;
};

class RawPlanet
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        std::string shaderDirectory;
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    static std::expected<std::unique_ptr<RawPlanet>, std::string> create(const CreateInfo& info);
    ~RawPlanet();
    RawPlanet(RawPlanet&&) noexcept;
    RawPlanet& operator=(RawPlanet&&) noexcept;
    RawPlanet(const RawPlanet&) = delete;
    RawPlanet& operator=(const RawPlanet&) = delete;

    void render(VkCommandBuffer cmd, const RawPlanetContext& context, const RawPlanetParams& params);
    [[nodiscard]] std::expected<void, std::string> recreate(const CreateInfo& info);

private:
    RawPlanet() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
