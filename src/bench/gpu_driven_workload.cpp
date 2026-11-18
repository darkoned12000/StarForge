#include "bench/gpu_driven_workload.hpp"

#include "bench/benchmark_scene.hpp"
#include "gpu_resources/image.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

namespace starforge::bench
{

namespace
{

constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

std::vector<std::uint8_t> readSpirv(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) return {};
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<std::uint8_t> bytes(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return bytes;
}

VkDeviceAddress addressOf(VkDevice device, VkBuffer buffer)
{
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(device, &info);
}

} // namespace

struct GpuDrivenWorkload::Impl
{
    GpuDrivenWorkloadConfig config;
    VkDevice device = VK_NULL_HANDLE;

    std::optional<gpu_resources::GpuCulling> culling;
    std::optional<gpu_resources::ComputePipeline> cullPipeline;
    std::optional<gpu_resources::GpuDrivenDraw> draw;
    std::vector<gpu_resources::Image> colorImages;
    std::vector<gpu_resources::Image::ImageView> colorViews;
    std::optional<vulkan::GpuTimer> cullTimer;

    float planes[24] = {};
    float viewProj[16] = {};
    VkDeviceAddress boundsAddress = 0;
    VkDeviceAddress indicesAddress = 0;
    std::vector<double> cullDurationsMs;

    void collectPrevious(std::uint32_t frameIndex)
    {
        if (!cullTimer->supported()) return;
        if (cullTimer->collectResults(frameIndex))
        {
            const double ns = cullTimer->scopeDurationNs(frameIndex, 0);
            if (ns >= 0.0) cullDurationsMs.push_back(ns / 1'000'000.0);
        }
    }
};

std::expected<GpuDrivenWorkload, std::string>
GpuDrivenWorkload::create(const vulkan::VulkanContext& context,
                          const vulkan::VulkanDevice& device,
                          gpu_resources::Allocator& allocator,
                          const GpuDrivenWorkloadConfig& config)
{
    if (config.instanceCount == 0 || config.framesInFlight == 0)
    {
        return std::unexpected("GpuDrivenWorkload requires non-zero instance and frame counts");
    }

    GpuDrivenWorkload workload;
    workload.impl_ = std::make_unique<Impl>();
    auto& impl = *workload.impl_;
    impl.config = config;
    impl.device = device.device();

    // Profile A scene: map visible fraction to radius, same as the CPU baseline.
    double radius = 5.0;
    if (config.visibleFraction > 0.0 && config.visibleFraction <= 1.0)
    {
        radius = 5.0 / std::cbrt(config.visibleFraction);
    }
    if (radius < 1.0) radius = 1.0;
    const auto scene = generateScene(config.seed, config.instanceCount, radius);

    std::vector<float> bounds(config.instanceCount * 4);
    for (std::uint32_t i = 0; i < config.instanceCount; ++i)
    {
        bounds[i * 4 + 0] = static_cast<float>(scene.instances[i].posX);
        bounds[i * 4 + 1] = static_cast<float>(scene.instances[i].posY);
        bounds[i * 4 + 2] = static_cast<float>(scene.instances[i].posZ);
        bounds[i * 4 + 3] = 0.5f * scene.instances[i].scale;
    }

    auto culling = gpu_resources::GpuCulling::create(allocator, {
        .instanceCount = config.instanceCount,
        .framesInFlight = config.framesInFlight,
        .name = "GpuDrivenWorkload",
    });
    if (!culling) return std::unexpected(culling.error());
    impl.culling.emplace(std::move(*culling));
    if (auto uploaded = impl.culling->uploadBounds(bounds); !uploaded)
    {
        return std::unexpected(uploaded.error());
    }

    const auto spirv = readSpirv(config.shaderDirectory + "/cull.spv");
    if (spirv.empty()) return std::unexpected("Missing cull.spv");
    auto cullPipeline = gpu_resources::ComputePipeline::create(device, {
        .spirv = spirv, .entryPoint = "main", .storageBufferCount = 3,
        .pushConstantSize = 104,
    });
    if (!cullPipeline) return std::unexpected(cullPipeline.error());
    impl.cullPipeline.emplace(std::move(*cullPipeline));

    const auto& caps = context.capabilities();
    auto draw = gpu_resources::GpuDrivenDraw::create({
        .device = device.device(),
        .physicalDevice = context.physicalDevice(),
        .queueFamily = device.graphicsQueueFamily(),
        .colorFormat = kColorFormat,
        .width = config.width,
        .height = config.height,
        .shaderDirectory = config.shaderDirectory,
        .bindless = caps.bindless,
        .drawIndirectCount = caps.drawIndirectCount,
    });
    if (!draw) return std::unexpected(draw.error());
    impl.draw.emplace(std::move(*draw));

    for (std::uint32_t i = 0; i < config.framesInFlight; ++i)
    {
        auto image = gpu_resources::Image::create(allocator, {
            .width = config.width,
            .height = config.height,
            .format = kColorFormat,
            .usage = gpu_resources::ImageUsageFlags::ColorAttachment,
        });
        if (!image) return std::unexpected(image.error());
        auto view = image->createImageView(device.device());
        if (!view) return std::unexpected(view.error());
        impl.colorImages.push_back(std::move(*image));
        impl.colorViews.push_back(std::move(*view));
    }

    vulkan::GpuTimer::CreateInfo timerInfo{};
    timerInfo.device = device.device();
    timerInfo.physicalDevice = context.physicalDevice();
    timerInfo.queueFamily = device.graphicsQueueFamily();
    timerInfo.framesInFlight = config.framesInFlight;
    timerInfo.maxScopesPerFrame = 2;
    auto timer = vulkan::GpuTimer::create(timerInfo);
    if (!timer) return std::unexpected(timer.error());
    impl.cullTimer.emplace(std::move(*timer));

    const auto view = glm::lookAt(starforge::math::Vec3d(0, 0, 5),
                                  starforge::math::Vec3d(0, 0, 0),
                                  starforge::math::Vec3d(0, 1, 0));
    const auto proj = starforge::math::perspectiveVulkanReverseZ(
        glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto frustum = buildFrustum(starforge::math::Mat4d(proj) * view);
    for (int p = 0; p < 6; ++p)
        for (int c = 0; c < 4; ++c)
            impl.planes[p * 4 + c] = static_cast<float>(frustum.planes[p][c]);

    const auto viewProj = starforge::math::Mat4f(starforge::math::Mat4d(proj) * view);
    std::memcpy(impl.viewProj, glm::value_ptr(viewProj), sizeof(impl.viewProj));

    impl.boundsAddress = addressOf(device.device(), impl.culling->boundsBuffer().handle());
    impl.indicesAddress = addressOf(device.device(), impl.culling->visibleIndicesBuffer(0).handle());

    return workload;
}

GpuDrivenWorkload::~GpuDrivenWorkload() = default;
GpuDrivenWorkload::GpuDrivenWorkload(GpuDrivenWorkload&&) noexcept = default;
GpuDrivenWorkload& GpuDrivenWorkload::operator=(GpuDrivenWorkload&&) noexcept = default;

void GpuDrivenWorkload::record(WorkloadContext& ctx)
{
    auto& impl = *impl_;
    const std::uint32_t frameIndex = ctx.frameIndex % impl.config.framesInFlight;
    impl.collectPrevious(frameIndex);

    if (impl.cullTimer->supported())
    {
        impl.cullTimer->resetQueryPool(ctx.cmd, frameIndex, 2);
        impl.cullTimer->beginScope(ctx.cmd, frameIndex, "gpu_cull");
    }
    if (auto recorded = impl.culling->recordCulling(ctx.cmd, impl.device, *impl.cullPipeline,
                                                   frameIndex, impl.planes,
                                                   impl.config.instanceCount);
        !recorded)
    {
        return;
    }
    if (impl.cullTimer->supported())
    {
        impl.cullTimer->endScope(ctx.cmd, frameIndex);
    }

    auto& color = impl.colorImages[frameIndex];
    color.transitionToColorAttachment(ctx.cmd);

    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = impl.colorViews[frameIndex].handle();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {impl.config.width, impl.config.height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(ctx.cmd, &rendering);

    impl.draw->record(ctx.cmd,
                      impl.culling->indirectBuffer(frameIndex).handle(),
                      impl.culling->boundsBuffer().handle(),
                      impl.culling->visibleIndicesBuffer(frameIndex).handle(),
                     impl.viewProj,
                     impl.boundsAddress,
                     impl.indicesAddress);
    vkCmdEndRendering(ctx.cmd);
}

double GpuDrivenWorkload::cullDurationMs() const
{
    if (impl_->cullDurationsMs.empty()) return 0.0;
    auto sorted = impl_->cullDurationsMs;
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
}

} // namespace starforge::bench
