#pragma once

#include "bridge_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge::public_api_bridge
{

struct RawRockInstance
{
    float position[3] = {0.0f, 0.0f, 0.0f};
    float scale = 1.0f;
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float tint[4] = {0.55f, 0.45f, 0.35f, 1.0f};
    std::uint32_t seed = 0;
    float pad[3] = {0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(RawRockInstance) == 64, "RawRockInstance must match rock_*.slang (64 B)");

struct RawRockRenderContext
{
    RawMat4f viewProj{};
    RawVec2f projectionScale{1.0f, 1.0f};
    RawVec2f resolution{1.0f, 1.0f};
    RawVec3f sunDirection{1.0f, 0.5f, 0.3f};
    RawVec3f cameraPosition{};
    float lodDistance = 1200.0f;
    float outlineWidthPixels = 1.2f;
    bool outline = true;
};

class RawAsteroidField
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        std::string shaderDirectory;
        std::uint32_t variantCount = 4;
        std::uint32_t capacity = 4096;
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    static std::expected<std::unique_ptr<RawAsteroidField>, std::string> create(const CreateInfo& info);
    ~RawAsteroidField();
    RawAsteroidField(RawAsteroidField&&) noexcept;
    RawAsteroidField& operator=(RawAsteroidField&&) noexcept;
    RawAsteroidField(const RawAsteroidField&) = delete;
    RawAsteroidField& operator=(const RawAsteroidField&) = delete;

    void render(VkCommandBuffer cmd, const RawRockRenderContext& context,
                const RawRockInstance* instances, std::uint32_t count);
    [[nodiscard]] std::expected<void, std::string> recreate(const CreateInfo& info);

private:
    RawAsteroidField() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
