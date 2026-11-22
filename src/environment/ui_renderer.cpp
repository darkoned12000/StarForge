#include "ui_renderer.hpp"

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "internal/effect_pipeline.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

struct Push
{
    std::uint64_t vertexAddress = 0;
};
static_assert(sizeof(Push) == 8);



} // namespace

struct UiRenderer::Impl
{
    VkDevice device = VK_NULL_HANDLE;
    std::unique_ptr<gpu_resources::Allocator> allocator;
    std::unique_ptr<gpu_resources::Buffer> vertexBuffer;
    VkDeviceSize capacity = 0;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline;

    ~Impl()
    {
        if (device != VK_NULL_HANDLE)
        {
            pipeline.reset();
            if (vert) vkDestroyShaderModule(device, vert, nullptr);
            if (frag) vkDestroyShaderModule(device, frag, nullptr);
            if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        }
    }
};

UiRenderer::~UiRenderer() = default;
UiRenderer::UiRenderer(UiRenderer&&) noexcept = default;
UiRenderer& UiRenderer::operator=(UiRenderer&&) noexcept = default;

std::expected<UiRenderer, std::string> UiRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.instance == VK_NULL_HANDLE ||
        info.physicalDevice == VK_NULL_HANDLE)
        return std::unexpected("UiRenderer requires instance/device/physicalDevice");

    using namespace starforge::gpu_resources;
    auto impl = std::make_unique<Impl>();
    impl->device = info.device;

    auto allocator = Allocator::create({
        .instance = info.instance, .physicalDevice = info.physicalDevice, .device = info.device});
    if (!allocator) return std::unexpected(allocator.error());
    impl->allocator = std::make_unique<Allocator>(std::move(*allocator));

    // Capacity for thousands of glyph quads: a busy HUD (panels + bars +
    // comms log + station buttons) can exceed 256 KB, which used to make
    // the whole UI silently vanish until a message expired.
    impl->capacity = 8 * 1024 * 1024;
    auto buffer = Buffer::create(*impl->allocator, {
        .size = impl->capacity, .usage = BufferUsage::Storage,
        .memory = MemoryPlacement::CpuToGpu});
    if (!buffer) return std::unexpected(buffer.error());
    impl->vertexBuffer = std::make_unique<Buffer>(std::move(*buffer));

    auto vert = internal::loadShaderModule(info.device, info.shaderDirectory + "/ui_text.vert.spv");
    auto frag = internal::loadShaderModule(info.device, info.shaderDirectory + "/ui_text.frag.spv");
    if (!vert || !frag)
        return std::unexpected(vert ? frag.error() : vert.error());
    impl->vert = *vert;
    impl->frag = *frag;

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = sizeof(Push);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(info.device, &layoutInfo, nullptr, &impl->layout) != VK_SUCCESS)
        return std::unexpected("UiRenderer: pipeline layout failed");

    auto pipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = impl->vert,
        .fragmentShader = impl->frag,
        .pipelineLayout = impl->layout,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = false,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_ALWAYS,
        .backfaceCulling = false,
        .blendEnable = true,
        .blendSrcFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .blendDstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    if (!pipeline) return std::unexpected(pipeline.error());
    impl->pipeline.emplace(std::move(*pipeline));

    UiRenderer result;
    result.impl_ = std::move(impl);
    return result;
}

void UiRenderer::render(VkCommandBuffer cmd, const std::vector<UiVertex>& vertices) const
{
    if (!impl_ || !impl_->pipeline || vertices.empty()) return;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(vertices.size() * sizeof(UiVertex));
    if (bytes > impl_->capacity)
    {
        // Grow rather than drop the frame: a busy HUD must never disappear.
        const VkDeviceSize needed = ((bytes / (2 * 1024 * 1024)) + 1) * 2 * 1024 * 1024;
        auto grown = gpu_resources::Buffer::create(*impl_->allocator, {
            .size = needed, .usage = gpu_resources::BufferUsage::Storage,
            .memory = gpu_resources::MemoryPlacement::CpuToGpu});
        if (!grown) return;
        // Avoid destroying a buffer still referenced by in-flight command buffers.
        vkDeviceWaitIdle(impl_->device);
        impl_->vertexBuffer = std::make_unique<gpu_resources::Buffer>(std::move(*grown));
        impl_->capacity = needed;
    }

    auto mapped = impl_->vertexBuffer->map();
    if (!mapped) return;
    std::memcpy(*mapped, vertices.data(), static_cast<std::size_t>(bytes));
    (void)impl_->vertexBuffer->flush();
    impl_->vertexBuffer->unmap();

    Push push{};
    push.vertexAddress = impl_->vertexBuffer->deviceAddress(impl_->device);
    vkCmdPushConstants(cmd, impl_->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    impl_->pipeline->bindAndDraw(cmd, static_cast<std::uint32_t>(vertices.size()), 0, 1);
}

} // namespace starforge::environment
