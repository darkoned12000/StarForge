#pragma once

#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace starforge::environment::internal
{

// Fullscreen post effect that samples one or two input images through a
// ring of descriptor sets (sampler + sampled image(s)), with optional push
// constants and additive or alpha blending. No depth read/write.
class TextureEffect
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED; // must match the active render pass
        std::string shaderDirectory;
        std::string shaderBaseName; // <name>.vert.spv / <name>.frag.spv
        std::uint32_t pushConstantSize = 0;
        bool additive = false;    // ONE/ONE blending for bloom accumulation
        bool alphaBlend = false;  // SRC_ALPHA / ONE_MINUS_SRC_ALPHA for overlays
    };

    static std::expected<TextureEffect, std::string> create(const CreateInfo& info);
    ~TextureEffect();
    TextureEffect(TextureEffect&&) noexcept;
    TextureEffect& operator=(TextureEffect&&) noexcept;
    TextureEffect(const TextureEffect&) = delete;
    TextureEffect& operator=(const TextureEffect&) = delete;

    // Records the fullscreen draw. Caller must have begun a rendering pass,
    // set viewport/scissor, and keep the input view alive for the frame.
    void render(VkCommandBuffer cmd, VkImageView inputView,
                const void* pushData = nullptr, std::uint32_t pushSize = 0,
                VkImageView secondView = VK_NULL_HANDLE) const;

private:
    TextureEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule vertexModule_ = VK_NULL_HANDLE;
    VkShaderModule fragmentModule_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    // Rotating sets: updating a bound descriptor set invalidates the command
    // buffer, so each render() call uses a different set.
    // Enough slots for per-frame-in-flight HDR/bloom renders without reusing a
    // descriptor set still in flight on the GPU.
    static constexpr std::uint32_t kSetCount = 16;
    mutable VkDescriptorSet descriptorSets_[kSetCount] = {};
    mutable std::uint32_t nextSet_ = 0;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    std::uint32_t pushConstantSize_ = 0;
};

} // namespace starforge::environment::internal
