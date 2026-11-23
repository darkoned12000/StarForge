#include "starforge/combat_fx.hpp"

#include "combat_fx_bridge.hpp"
#include "convert.hpp"
#include "renderer_impl.hpp"
#include "shader_dir.hpp"

#include <string>
#include <vector>

namespace starforge
{
namespace
{
namespace bridge = public_api_bridge;
using public_api_convert::toRaw;

[[nodiscard]] std::string resolveFxShaderDirectory(const std::string& requested)
{
    return public_api_detail::resolveShaderDirectory(requested, "combat_fx.vert.spv");
}

[[nodiscard]] bridge::RawCombatFx::CreateInfo
makeBackendInfo(Renderer& renderer, const std::string& shaderDirectory,
                const CombatFx::CreateInfo& info)
{
    return bridge::RawCombatFx::CreateInfo{
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .colorFormat = RendererInternalAccess::sceneColorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = shaderDirectory,
        .specializationInfo = RendererInternalAccess::specializationInfo(renderer),
        .capacity = info.capacity,
    };
}
} // namespace

struct CombatFx::Impl
{
    std::unique_ptr<bridge::RawCombatFx> backend;
    std::string shaderDirectory;
    CreateInfo createInfo;
    // Reused every frame; the whole point of the fixed-capacity pool is
    // that recording an effect never allocates.
    std::vector<bridge::RawCombatFxInstance> rawInstances;
};

CombatFx::CombatFx(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
CombatFx::~CombatFx() = default;
CombatFx::CombatFx(CombatFx&&) noexcept = default;
CombatFx& CombatFx::operator=(CombatFx&&) noexcept = default;

std::expected<CombatFx, std::string>
CombatFx::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
        return std::unexpected("CombatFx::create failed: Renderer is invalid or moved-from");
    const std::string dir = resolveFxShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawCombatFx::create(makeBackendInfo(renderer, dir, info));
    if (!backend) return std::unexpected(backend.error());
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = dir;
    impl->createInfo = info;
    impl->rawInstances.resize(info.capacity);
    return CombatFx(std::move(impl));
}

void CombatFx::record(FrameContext& frameContext, const RenderContext& context,
                      const Instance* instances, std::uint32_t count)
{
    if (!impl_ || !impl_->backend) return;

    bridge::RawCombatFxContext raw;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            raw.viewProj[c * 4 + r] = context.viewProj.columns[c][r];
    raw.cameraRight[0] = context.cameraRight.x();
    raw.cameraRight[1] = context.cameraRight.y();
    raw.cameraRight[2] = context.cameraRight.z();
    raw.cameraUp[0] = context.cameraUp.x();
    raw.cameraUp[1] = context.cameraUp.y();
    raw.cameraUp[2] = context.cameraUp.z();
    raw.resolution[0] = context.resolution.x();
    raw.resolution[1] = context.resolution.y();
    raw.cameraPosition[0] = context.cameraPosition.x();
    raw.cameraPosition[1] = context.cameraPosition.y();
    raw.cameraPosition[2] = context.cameraPosition.z();

    auto& rawInstances = impl_->rawInstances;
    rawInstances.clear();
    if (instances && count > 0)
    {
        rawInstances.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto& src = instances[i];
            bridge::RawCombatFxInstance dst;
            dst.posRadius[0] = src.position.x();
            dst.posRadius[1] = src.position.y();
            dst.posRadius[2] = src.position.z();
            dst.posRadius[3] = src.radius;
            dst.direction[0] = src.direction.x();
            dst.direction[1] = src.direction.y();
            dst.direction[2] = src.direction.z();
            dst.direction[3] = static_cast<float>(src.kind);
            dst.color[0] = src.color.x();
            dst.color[1] = src.color.y();
            dst.color[2] = src.color.z();
            dst.color[3] = src.color.w();
            dst.misc[0] = src.age;
            dst.misc[1] = src.seed;
            dst.misc[2] = src.aspect;
            dst.misc[3] = src.fade;
            rawInstances.push_back(dst);
        }
    }
    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), raw,
                           rawInstances.empty() ? nullptr : rawInstances.data(), count);
}

std::expected<void, std::string> CombatFx::recreate(Renderer& renderer)
{
    if (!impl_ || !impl_->backend)
        return std::unexpected("CombatFx is moved-from");
    renderer.waitIdle();
    auto recreated = create(renderer, impl_->createInfo);
    if (!recreated) return std::unexpected(recreated.error());
    impl_->backend = std::move(recreated->impl_->backend);
    return {};
}

} // namespace starforge
