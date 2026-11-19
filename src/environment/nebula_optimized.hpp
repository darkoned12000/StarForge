#pragma once

#include "internal/texture_effect.hpp"
#include "math/math.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace starforge::environment
{

struct NebulaOptContext
{
    math::Vec4f right{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4f up{0.0f, 1.0f, 0.0f, 0.0f};
    math::Vec4f forward{0.0f, 0.0f, 1.0f, 0.0f};
    math::Vec2f aspectTan{1.0f, 1.0f};
    float style = 0.0f; // >0.5 -> cel-styled bands
};

struct NebulaOptParams
{
    float time = 0.0f;
    float density = 1.0f;
    float scattering = 0.5f;
    float absorption = 0.1f;
    math::Vec4f color{0.5f, 0.2f, 0.8f, 1.0f};
};

// Seeded 3D-noise-textured nebula with a half-resolution offscreen target.
// The 64^3 volume replaces ~300 hash evals/pixel; rendering at 1/2 x 1/2
// resolution cuts nebula fill by 75%, then a bilinear blit upsamples into the
// active frame. Both optimisations are headless-verifiable.
class NebulaOptimized
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t width = 128;
        std::uint32_t height = 128;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        std::string shaderDirectory;
        std::uint32_t seed = 42;
        std::uint32_t volumeSize = 64;
    };

    static std::expected<NebulaOptimized, std::string> create(const CreateInfo& info);
    ~NebulaOptimized();
    NebulaOptimized(NebulaOptimized&&) noexcept;
    NebulaOptimized& operator=(NebulaOptimized&&) noexcept;
    NebulaOptimized(const NebulaOptimized&) = delete;
    NebulaOptimized& operator=(const NebulaOptimized&) = delete;

    // Renders the nebula into the internal half-resolution target. Call this
    // before beginning the final rendering pass.
    void renderLowRes(VkCommandBuffer cmd, const NebulaOptContext& ctx, const NebulaOptParams& params);

    // Renders the same shader at full resolution into `view` (parity proof).
    void renderFullRes(VkCommandBuffer cmd, const NebulaOptContext& ctx, const NebulaOptParams& params,
                       VkImageView view, std::uint32_t width, std::uint32_t height);

    // Blits the half-res target into the active rendering pass.
    void upsample(VkCommandBuffer cmd, std::uint32_t width, std::uint32_t height) const;

    [[nodiscard]] VkImageView halfResView() const noexcept;
    [[nodiscard]] std::uint32_t halfResWidth() const noexcept;
    [[nodiscard]] std::uint32_t halfResHeight() const noexcept;
    [[nodiscard]] VkImage halfResHandle() const noexcept;

private:
    NebulaOptimized() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::environment
