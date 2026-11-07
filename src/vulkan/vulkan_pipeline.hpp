#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::vulkan
{

// Configuration for a graphics pipeline using dynamic rendering.
struct GraphicsPipelineConfig
{
    VkDevice device = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool depthTest = true;
    bool depthWrite = true;
    VkCompareOp depthCompare = VK_COMPARE_OP_GREATER;
    bool backfaceCulling = true;
    // Additive/translucent blending for overlays (default: off, opaque).
    bool blendEnable = false;
    VkBlendFactor blendSrcFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor blendDstFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor blendSrcAlphaFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor blendDstAlphaFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
};

// RAII wrapper for a graphics pipeline (dynamic rendering, no render passes).
class GraphicsPipeline
{
public:
    [[nodiscard]] static std::expected<GraphicsPipeline, std::string>
    create(const GraphicsPipelineConfig& config);

    ~GraphicsPipeline();
    GraphicsPipeline(GraphicsPipeline&& other) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    [[nodiscard]] VkPipeline handle() const noexcept { return pipeline_; }

    // Record a draw call with the given vertex/index counts and BDA addresses.
    void bindAndDraw(VkCommandBuffer cmd, std::uint32_t vertexCount,
                     std::uint32_t indexCount, std::uint32_t instanceCount = 1) const;

private:
    GraphicsPipeline() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

// Helper to create a pipeline layout with push constants.
[[nodiscard]] std::expected<VkPipelineLayout, std::string>
createPushConstantPipelineLayout(VkDevice device, std::uint32_t pushConstantSize);

} // namespace starforge::vulkan
