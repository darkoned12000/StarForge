#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge::public_api_bridge
{

struct RawCombatFxInstance
{
    float posRadius[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float direction[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // w = kind (0..4)
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float misc[4] = {0.0f, 0.0f, 1.0f, 1.0f};     // age, seed, aspect, fade
};
static_assert(sizeof(RawCombatFxInstance) == 64, "RawCombatFxInstance must match combat_fx.slang");

struct RawCombatFxContext
{
    float viewProj[16] = {};
    float cameraRight[3] = {};
    float cameraUp[3] = {};
    float resolution[2] = {};
    float cameraPosition[3] = {};
};

class RawCombatFx
{
public:
    RawCombatFx() = default;
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        std::string shaderDirectory;
        const VkSpecializationInfo* specializationInfo = nullptr;
        std::uint32_t capacity = 4096;
    };

    ~RawCombatFx();
    RawCombatFx(RawCombatFx&&) noexcept;
    RawCombatFx& operator=(RawCombatFx&&) noexcept;
    RawCombatFx(const RawCombatFx&) = delete;
    RawCombatFx& operator=(const RawCombatFx&) = delete;

    [[nodiscard]] static std::expected<std::unique_ptr<RawCombatFx>, std::string>
    create(const CreateInfo& info);

    void render(VkCommandBuffer cmd, const RawCombatFxContext& context,
                const RawCombatFxInstance* instances, std::uint32_t count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
