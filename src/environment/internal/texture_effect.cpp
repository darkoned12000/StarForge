#include "texture_effect.hpp"

#include "effect_pipeline.hpp"

#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment::internal
{



std::expected<TextureEffect, std::string> TextureEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.shaderBaseName.empty())
        return std::unexpected("TextureEffect requires a device and shader base name");

    TextureEffect effect;
    effect.device_ = info.device;
    effect.pushConstantSize_ = info.pushConstantSize;
    auto vertModule = loadShaderModule(info.device, info.shaderDirectory + "/" + info.shaderBaseName + ".vert.spv");
    auto fragModule = loadShaderModule(info.device, info.shaderDirectory + "/" + info.shaderBaseName + ".frag.spv");
    if (!vertModule || !fragModule)
    {
        effect.destroy();
        return std::unexpected(vertModule ? fragModule.error() : vertModule.error());
    }
    effect.vertexModule_ = *vertModule;
    effect.fragmentModule_ = *fragModule;

    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(info.device, &layoutInfo, nullptr, &effect.descriptorSetLayout_) != VK_SUCCESS)
    {
        effect.destroy();
        return std::unexpected("TextureEffect: vkCreateDescriptorSetLayout failed");
    }

    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sizes[0].descriptorCount = kSetCount * 2;
    sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    sizes[1].descriptorCount = kSetCount * 2;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kSetCount;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(info.device, &poolInfo, nullptr, &effect.descriptorPool_) != VK_SUCCESS)
    {
        effect.destroy();
        return std::unexpected("TextureEffect: vkCreateDescriptorPool failed");
    }
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = effect.descriptorPool_;
    VkDescriptorSetLayout layouts[kSetCount]{};
    for (std::uint32_t i = 0; i < kSetCount; ++i)
    {
        layouts[i] = effect.descriptorSetLayout_;
    }
    allocateInfo.descriptorSetCount = kSetCount;
    allocateInfo.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(info.device, &allocateInfo, effect.descriptorSets_) != VK_SUCCESS)
    {
        effect.destroy();
        return std::unexpected("TextureEffect: vkAllocateDescriptorSets failed");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(info.device, &samplerInfo, nullptr, &effect.sampler_) != VK_SUCCESS)
    {
        effect.destroy();
        return std::unexpected("TextureEffect: vkCreateSampler failed");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &effect.descriptorSetLayout_;
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = info.pushConstantSize;
    if (info.pushConstantSize > 0)
    {
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &push;
    }
    if (vkCreatePipelineLayout(info.device, &pipelineLayoutInfo, nullptr, &effect.pipelineLayout_) != VK_SUCCESS)
    {
        effect.destroy();
        return std::unexpected("TextureEffect: vkCreatePipelineLayout failed");
    }

    auto pipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = effect.vertexModule_,
        .fragmentShader = effect.fragmentModule_,
        .pipelineLayout = effect.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = false,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_ALWAYS,
        .backfaceCulling = false,
        .blendEnable = info.additive || info.alphaBlend,
        .blendSrcFactor = info.alphaBlend ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE,
        .blendDstFactor = info.alphaBlend ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                                          : VK_BLEND_FACTOR_ONE,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    if (!pipeline)
    {
        auto error = pipeline.error();
        effect.destroy();
        return std::unexpected(error);
    }
    effect.pipeline_.emplace(std::move(*pipeline));
    return effect;
}

TextureEffect::~TextureEffect() { destroy(); }
TextureEffect::TextureEffect(TextureEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      vertexModule_(std::exchange(other.vertexModule_, VK_NULL_HANDLE)),
      fragmentModule_(std::exchange(other.fragmentModule_, VK_NULL_HANDLE)),
      descriptorSetLayout_(std::exchange(other.descriptorSetLayout_, VK_NULL_HANDLE)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE)),
      nextSet_(other.nextSet_),
      sampler_(std::exchange(other.sampler_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::move(other.pipeline_)),
      pushConstantSize_(other.pushConstantSize_)
{
    for (std::uint32_t i = 0; i < kSetCount; ++i)
    {
        descriptorSets_[i] = std::exchange(other.descriptorSets_[i], VK_NULL_HANDLE);
        boundViews_[i] = std::exchange(other.boundViews_[i], VK_NULL_HANDLE);
        boundSecondViews_[i] = std::exchange(other.boundSecondViews_[i], VK_NULL_HANDLE);
    }
}
TextureEffect& TextureEffect::operator=(TextureEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        vertexModule_ = std::exchange(other.vertexModule_, VK_NULL_HANDLE);
        fragmentModule_ = std::exchange(other.fragmentModule_, VK_NULL_HANDLE);
        descriptorSetLayout_ = std::exchange(other.descriptorSetLayout_, VK_NULL_HANDLE);
        descriptorPool_ = std::exchange(other.descriptorPool_, VK_NULL_HANDLE);
        nextSet_ = other.nextSet_;
        for (std::uint32_t i = 0; i < kSetCount; ++i)
        {
            descriptorSets_[i] = std::exchange(other.descriptorSets_[i], VK_NULL_HANDLE);
            boundViews_[i] = std::exchange(other.boundViews_[i], VK_NULL_HANDLE);
            boundSecondViews_[i] = std::exchange(other.boundSecondViews_[i], VK_NULL_HANDLE);
        }
        sampler_ = std::exchange(other.sampler_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        pipeline_ = std::move(other.pipeline_);
        pushConstantSize_ = other.pushConstantSize_;
    }
    return *this;
}

void TextureEffect::destroy() noexcept
{
    pipeline_.reset();
    if (device_ != VK_NULL_HANDLE)
    {
        if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
        if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (descriptorSetLayout_) vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        if (vertexModule_) vkDestroyShaderModule(device_, vertexModule_, nullptr);
        if (fragmentModule_) vkDestroyShaderModule(device_, fragmentModule_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    nextSet_ = 0;
    for (std::uint32_t i = 0; i < kSetCount; ++i)
    {
        descriptorSets_[i] = VK_NULL_HANDLE;
        boundViews_[i] = VK_NULL_HANDLE;
        boundSecondViews_[i] = VK_NULL_HANDLE;
    }
    vertexModule_ = VK_NULL_HANDLE;
    fragmentModule_ = VK_NULL_HANDLE;
}

void TextureEffect::render(VkCommandBuffer cmd, VkImageView inputView,
                           const void* pushData, std::uint32_t pushSize,
                           VkImageView secondView) const
{
    if (!pipeline_ || inputView == VK_NULL_HANDLE) return;
    const std::uint32_t slot = nextSet_++ % kSetCount;
    const VkDescriptorSet set = descriptorSets_[slot];
    const VkImageView effectiveSecondView = secondView != VK_NULL_HANDLE ? secondView : inputView;
    if (boundViews_[slot] != inputView || boundSecondViews_[slot] != effectiveSecondView)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = inputView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo secondInfo{};
        secondInfo.imageView = effectiveSecondView;
        secondInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = sampler_;
        VkWriteDescriptorSet writes[4]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[0].pImageInfo = &imageInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[1].pImageInfo = &samplerInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = set;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[2].pImageInfo = &secondInfo;
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = set;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[3].pImageInfo = &samplerInfo;
        vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
        boundViews_[slot] = inputView;
        boundSecondViews_[slot] = effectiveSecondView;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &set, 0, nullptr);
    if (pushConstantSize_ > 0 && pushData != nullptr && pushSize > 0)
    {
        const std::uint32_t size = std::min(pushSize, pushConstantSize_);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, pushData);
    }
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace starforge::environment::internal
