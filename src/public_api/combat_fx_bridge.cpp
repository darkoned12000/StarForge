#include "combat_fx_bridge.hpp"

#include "environment/combat_fx_renderer.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace starforge::public_api_bridge
{

namespace
{

using namespace starforge;
using namespace starforge::gpu_resources;

std::expected<std::unique_ptr<Buffer>, std::string>
upload(Allocator& allocator, const void* data, VkDeviceSize bytes)
{
    auto buffer = Buffer::create(allocator, {.size = bytes, .usage = BufferUsage::Storage,
                                             .memory = MemoryPlacement::CpuToGpu,
                                             .name = "starforge.CombatFx.Instances"});
    if (!buffer) return std::unexpected(buffer.error());
    auto mapped = buffer->map();
    if (!mapped) return std::unexpected(mapped.error());
    std::memcpy(*mapped, data, static_cast<std::size_t>(bytes));
    (void)buffer->flush();
    buffer->unmap();
    return std::make_unique<Buffer>(std::move(*buffer));
}

} // namespace

struct RawCombatFx::Impl
{
    VkDevice device = VK_NULL_HANDLE;
    std::unique_ptr<Allocator> allocator;
    std::unique_ptr<Buffer> instances;
    std::uint64_t instanceAddress = 0;
    std::uint32_t capacity = 0;
    std::optional<environment::CombatFxRenderer> renderer;
};

RawCombatFx::~RawCombatFx() = default;
RawCombatFx::RawCombatFx(RawCombatFx&&) noexcept = default;
RawCombatFx& RawCombatFx::operator=(RawCombatFx&&) noexcept = default;

std::expected<std::unique_ptr<RawCombatFx>, std::string>
RawCombatFx::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.instance == VK_NULL_HANDLE ||
        info.physicalDevice == VK_NULL_HANDLE)
        return std::unexpected("RawCombatFx requires instance/device/physicalDevice");

    auto allocator = Allocator::create({.instance = info.instance,
                                        .physicalDevice = info.physicalDevice,
                                        .device = info.device});
    if (!allocator) return std::unexpected(allocator.error());

    auto renderer = environment::CombatFxRenderer::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
        .specializationInfo = info.specializationInfo,
    });
    if (!renderer) return std::unexpected(renderer.error());

    // Allocate once. No buffer is destroyed on the render path, so a frame
    // in flight can never read freed GPU memory (the M17 device-loss rule).
    const std::uint32_t capacity = std::max(1u, info.capacity);
    std::vector<RawCombatFxInstance> initial(capacity);
    auto buffer = upload(*allocator, initial.data(),
                         static_cast<VkDeviceSize>(initial.size() * sizeof(RawCombatFxInstance)));
    if (!buffer) return std::unexpected(buffer.error());

    auto result = std::unique_ptr<RawCombatFx>(new RawCombatFx());
    auto impl = std::make_unique<Impl>();
    impl->device = info.device;
    impl->allocator = std::make_unique<Allocator>(std::move(*allocator));
    impl->instances = std::move(*buffer);
    impl->instanceAddress = impl->instances->deviceAddress(info.device);
    impl->capacity = capacity;
    impl->renderer = std::move(*renderer);
    result->impl_ = std::move(impl);
    return result;
}

void RawCombatFx::render(VkCommandBuffer cmd, const RawCombatFxContext& context,
                         const RawCombatFxInstance* instances, std::uint32_t count)
{
    if (!impl_) return;
    auto& impl = *impl_;
    count = std::min(count, impl.capacity);
    if (count == 0 || instances == nullptr) return;

    {
        auto mapped = impl.instances->map();
        if (!mapped) return;
        std::memcpy(*mapped, instances, static_cast<std::size_t>(count) * sizeof(RawCombatFxInstance));
        (void)impl.instances->flush();
        impl.instances->unmap();
    }

    environment::CombatFxRenderContext rctx;
    std::memcpy(&rctx.viewProj, context.viewProj, sizeof(rctx.viewProj));
    rctx.cameraRight = math::Vec4f{context.cameraRight[0], context.cameraRight[1],
                                   context.cameraRight[2], 0.0f};
    rctx.cameraUp = math::Vec4f{context.cameraUp[0], context.cameraUp[1],
                                context.cameraUp[2], 0.0f};
    rctx.resolution = math::Vec2f{context.resolution[0], context.resolution[1]};
    rctx.cameraPosition = math::Vec3f{context.cameraPosition[0],
                                      context.cameraPosition[1],
                                      context.cameraPosition[2]};

    impl.renderer->render(cmd, rctx, environment::CombatFxDrawParams{
        .instanceAddress = impl.instanceAddress,
        .instanceCount = count,
    });
}

} // namespace starforge::public_api_bridge
