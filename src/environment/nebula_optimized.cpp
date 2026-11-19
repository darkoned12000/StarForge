#include "nebula_optimized.hpp"

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

struct NebulaPush
{
    float time = 0.0f, density = 1.0f, scattering = 0.5f, absorption = 0.1f;
    float color[4] = {0.5f, 0.2f, 0.8f, 1.0f};
    float right[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    float forward[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    float aspectTan[4] = {1.0f, 1.0f, 0.0f, 0.0f};
};
static_assert(sizeof(NebulaPush) == 96, "NebulaPush must match nebula_noise3d.slang");

VkShaderModule loadModule(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) return VK_NULL_HANDLE;
    const auto size = static_cast<std::size_t>(file.tellg());
    if (size < 20 || (size % 4) != 0) return VK_NULL_HANDLE;
    std::vector<std::uint32_t> words(size / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS ? module : VK_NULL_HANDLE;
}

std::uint8_t seededNoise(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t seed)
{
    std::uint32_t h = x * 374761393u + y * 668265263u + z * 2147483647u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<std::uint8_t>(h & 0xFFu);
}

} // namespace

struct NebulaOptimized::Impl
{
    VkDevice device = VK_NULL_HANDLE;
    std::unique_ptr<gpu_resources::Allocator> allocator;
    std::unique_ptr<gpu_resources::Image> volume;
    gpu_resources::Image::ImageView volumeView;
    std::unique_ptr<gpu_resources::Buffer> staging;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline;
    std::unique_ptr<gpu_resources::Image> halfRes;
    gpu_resources::Image::ImageView halfResView;
    std::optional<internal::TextureEffect> blit;
    std::uint32_t halfWidth = 0, halfHeight = 0;
    std::uint32_t volumeSize = 64;
    bool uploaded = false;
    std::uint8_t sampleByte = 0;
    VkImageLayout halfResLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    ~Impl()
    {
        if (device != VK_NULL_HANDLE)
        {
            pipeline.reset();
            blit.reset();
            if (vert) vkDestroyShaderModule(device, vert, nullptr);
            if (frag) vkDestroyShaderModule(device, frag, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (descriptorLayout) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            if (sampler) vkDestroySampler(device, sampler, nullptr);
        }
    }
};

NebulaOptimized::~NebulaOptimized() = default;
NebulaOptimized::NebulaOptimized(NebulaOptimized&&) noexcept = default;
NebulaOptimized& NebulaOptimized::operator=(NebulaOptimized&&) noexcept = default;

std::expected<NebulaOptimized, std::string> NebulaOptimized::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.instance == VK_NULL_HANDLE ||
        info.physicalDevice == VK_NULL_HANDLE)
        return std::unexpected("NebulaOptimized requires instance/device/physicalDevice");

    using namespace starforge::gpu_resources;
    auto impl = std::make_unique<Impl>();
    impl->device = info.device;
    impl->volumeSize = info.volumeSize;
    impl->halfWidth = std::max(1u, info.width / 2);
    impl->halfHeight = std::max(1u, info.height / 2);

    auto allocator = Allocator::create({
        .instance = info.instance, .physicalDevice = info.physicalDevice, .device = info.device});
    if (!allocator) return std::unexpected(allocator.error());
    impl->allocator = std::make_unique<Allocator>(std::move(*allocator));

    const std::uint32_t vs = info.volumeSize;
    auto volume = Image::create(*impl->allocator, {
        .width = vs, .height = vs, .depth = vs, .imageType = VK_IMAGE_TYPE_3D,
        .format = VK_FORMAT_R8_UNORM, .usage = ImageUsageFlags::Sampled});
    if (!volume) return std::unexpected(volume.error());
    auto volumeView = volume->createImageView(info.device);
    if (!volumeView) return std::unexpected(volumeView.error());
    impl->volume = std::make_unique<Image>(std::move(*volume));
    impl->volumeView = std::move(*volumeView);

    std::vector<std::uint8_t> noise(static_cast<std::size_t>(vs) * vs * vs);
    for (std::uint32_t z = 0; z < vs; ++z)
        for (std::uint32_t y = 0; y < vs; ++y)
            for (std::uint32_t x = 0; x < vs; ++x)
                noise[(static_cast<std::size_t>(z) * vs + y) * vs + x] =
                    seededNoise(x, y, z, info.seed);
    auto staging = Buffer::create(*impl->allocator, {
        .size = noise.size(), .usage = BufferUsage::TransferSrc,
        .memory = MemoryPlacement::CpuToGpu});
    if (!staging) return std::unexpected(staging.error());
    auto mapped = staging->map();
    if (!mapped) return std::unexpected(mapped.error());
    std::memcpy(*mapped, noise.data(), noise.size());
    (void)staging->flush();
    staging->unmap();
    impl->sampleByte = noise[100];
    impl->staging = std::make_unique<Buffer>(std::move(*staging));

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (vkCreateSampler(info.device, &samplerInfo, nullptr, &impl->sampler) != VK_SUCCESS)
        return std::unexpected("NebulaOptimized: vkCreateSampler failed");

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(info.device, &layoutInfo, nullptr, &impl->descriptorLayout) != VK_SUCCESS)
        return std::unexpected("NebulaOptimized: vkCreateDescriptorSetLayout failed");

    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    sizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(info.device, &poolInfo, nullptr, &impl->descriptorPool) != VK_SUCCESS)
        return std::unexpected("NebulaOptimized: vkCreateDescriptorPool failed");
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = impl->descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &impl->descriptorLayout;
    if (vkAllocateDescriptorSets(info.device, &allocInfo, &impl->descriptorSet) != VK_SUCCESS)
        return std::unexpected("NebulaOptimized: vkAllocateDescriptorSets failed");

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = impl->volumeView.handle();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo samplerInfoDesc{};
    samplerInfoDesc.sampler = impl->sampler;
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = impl->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = &imageInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = impl->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = &samplerInfoDesc;
    vkUpdateDescriptorSets(info.device, 2, writes, 0, nullptr);

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(NebulaPush);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &impl->descriptorLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(info.device, &pipelineLayoutInfo, nullptr, &impl->pipelineLayout) != VK_SUCCESS)
        return std::unexpected("NebulaOptimized: vkCreatePipelineLayout failed");

    impl->vert = loadModule(info.device, info.shaderDirectory + "/nebula_noise3d.vert.spv");
    impl->frag = loadModule(info.device, info.shaderDirectory + "/nebula_noise3d.frag.spv");
    if (!impl->vert || !impl->frag)
        return std::unexpected("NebulaOptimized: missing nebula_noise3d shaders");

    auto pipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = VK_FORMAT_UNDEFINED,
        .vertexShader = impl->vert,
        .fragmentShader = impl->frag,
        .pipelineLayout = impl->pipelineLayout,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = false,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_ALWAYS,
        .backfaceCulling = false,
        .blendEnable = true,
        .blendSrcFactor = VK_BLEND_FACTOR_ONE,
        .blendDstFactor = VK_BLEND_FACTOR_ONE,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    if (!pipeline) return std::unexpected(pipeline.error());
    impl->pipeline.emplace(std::move(*pipeline));

    auto halfRes = Image::create(*impl->allocator, {
        .width = impl->halfWidth, .height = impl->halfHeight,
        .format = info.colorFormat,
        .usage = ImageUsageFlags::ColorAttachment | ImageUsageFlags::Sampled});
    if (!halfRes) return std::unexpected(halfRes.error());
    auto halfView = halfRes->createImageView(info.device);
    if (!halfView) return std::unexpected(halfView.error());
    impl->halfRes = std::make_unique<Image>(std::move(*halfRes));
    impl->halfResView = std::move(*halfView);

    auto blit = internal::TextureEffect::create({
        .device = info.device, .colorFormat = info.colorFormat,
        .shaderDirectory = info.shaderDirectory, .shaderBaseName = "texture_blit"});
    if (!blit) return std::unexpected(blit.error());
    impl->blit.emplace(std::move(*blit));

    NebulaOptimized result;
    result.impl_ = std::move(impl);
    return result;
}

NebulaPush makePush(const NebulaOptContext& ctx, const NebulaOptParams& params)
{
    NebulaPush push;
    push.time = params.time;
    push.density = params.density;
    push.scattering = params.scattering;
    push.absorption = params.absorption;
    const math::Vec4f vecs[4] = {params.color, ctx.right, ctx.up, ctx.forward};
    float* dests[4] = {push.color, push.right, push.up, push.forward};
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 4; ++c)
            dests[i][c] = vecs[i][c];
    push.aspectTan[0] = ctx.aspectTan.x;
    push.aspectTan[1] = ctx.aspectTan.y;
    push.aspectTan[2] = ctx.style;
    return push;
}

void NebulaOptimized::renderLowRes(VkCommandBuffer cmd, const NebulaOptContext& ctx,
                                   const NebulaOptParams& params)
{
    if (!impl_ || !impl_->pipeline) return;
    auto& impl = *impl_;

    if (!impl.uploaded)
    {
        gpu_resources::Image::transitionLayout(cmd, impl.volume->handle(), VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
                                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {impl.volumeSize, impl.volumeSize, impl.volumeSize};
        vkCmdCopyBufferToImage(cmd, impl.staging->handle(), impl.volume->handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        gpu_resources::Image::transitionLayout(cmd, impl.volume->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        impl.uploaded = true;
    }

    gpu_resources::Image::transitionLayout(cmd, impl.halfRes->handle(), impl.halfResLayout,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    impl.halfResLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = impl.halfResView.handle();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {impl.halfWidth, impl.halfHeight};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(cmd, &rendering);
    VkViewport viewport{0.0f, static_cast<float>(impl.halfHeight),
                        static_cast<float>(impl.halfWidth), -static_cast<float>(impl.halfHeight),
                        0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {impl.halfWidth, impl.halfHeight}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    NebulaPush push = makePush(ctx, params);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipeline->handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipelineLayout, 0, 1,
                            &impl.descriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, impl.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    gpu_resources::Image::transitionLayout(cmd, impl.halfRes->handle(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_SHADER_READ_BIT);
    impl.halfResLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void NebulaOptimized::renderFullRes(VkCommandBuffer cmd, const NebulaOptContext& ctx,
                                    const NebulaOptParams& params, VkImageView view,
                                    std::uint32_t width, std::uint32_t height)
{
    if (!impl_ || !impl_->pipeline) return;
    auto& impl = *impl_;
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {width, height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(cmd, &rendering);
    VkViewport viewport{0.0f, static_cast<float>(height), static_cast<float>(width),
                        -static_cast<float>(height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    NebulaPush push = makePush(ctx, params);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipeline->handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipelineLayout, 0, 1,
                            &impl.descriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, impl.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

void NebulaOptimized::upsample(VkCommandBuffer cmd, std::uint32_t width,
                               std::uint32_t height) const
{
    if (!impl_ || !impl_->blit) return;
    VkViewport viewport{0.0f, static_cast<float>(height), static_cast<float>(width),
                        -static_cast<float>(height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    impl_->blit->render(cmd, impl_->halfResView.handle());
}

VkImageView NebulaOptimized::halfResView() const noexcept
{
    return impl_ ? impl_->halfResView.handle() : VK_NULL_HANDLE;
}

std::uint32_t NebulaOptimized::halfResWidth() const noexcept
{
    return impl_ ? impl_->halfWidth : 0;
}

std::uint32_t NebulaOptimized::halfResHeight() const noexcept
{
    return impl_ ? impl_->halfHeight : 0;
}

VkImage NebulaOptimized::halfResHandle() const noexcept
{
    return impl_ ? impl_->halfRes->handle() : VK_NULL_HANDLE;
}

} // namespace starforge::environment
