#include "particle_system.hpp"

#include "internal/effect_pipeline.hpp"

#include <cstddef>
#include <algorithm>
#include <utility>

namespace starforge::environment
{

namespace
{

std::uint32_t hash32(std::uint32_t x)
{
    x = x * 1664525u + 1013904223u;
    x ^= x >> 16u;
    return x;
}

} // namespace

std::expected<ParticleSystem, std::string> ParticleSystem::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("ParticleSystem: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("ParticleSystem: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("ParticleSystem: shaderDirectory is empty");
    }

    ParticleSystem system;
    system.device_ = info.device;

    auto effect = internal::BillboardEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "billboard",
        .pushConstantSize = sizeof(internal::BillboardPush),
    });
    if (!effect)
    {
        return std::unexpected(std::move(effect.error()));
    }
    system.effect_ = std::move(*effect);

    return system;
}

ParticleSystem::~ParticleSystem()
{
    destroy();
}

ParticleSystem::ParticleSystem(ParticleSystem&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

ParticleSystem& ParticleSystem::operator=(ParticleSystem&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void ParticleSystem::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void ParticleSystem::update(float /*deltaTime*/)
{
    // Particle simulation state lives GPU-side in a future buffer; this
    // CPU hook is reserved for emitter bookkeeping.
}

void ParticleSystem::render(VkCommandBuffer cmd, const ParticleRenderContext& context, const ParticleParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    if (params.count == 0)
    {
        return;
    }

    const float aspect = context.resolution.x / std::max(context.resolution.y, 1.0f);
    const std::uint32_t count = params.count < 256 ? params.count : 256;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        // Deterministic per-particle 3D spherical spread, advected by
        // velocity over the particle lifetime (no razor-thin plane).
        const auto hx = static_cast<float>(hash32(i * 3u + 0u)) / 4294967296.0f;
        const auto hy = static_cast<float>(hash32(i * 3u + 1u)) / 4294967296.0f;
        const auto hz = static_cast<float>(hash32(i * 3u + 2u)) / 4294967296.0f;
        const math::Vec3f offset{(hx - 0.5f) * 2.0f, (hy - 0.5f) * 2.0f, (hz - 0.5f) * 2.0f};
        const math::Vec3f worldPos =
            params.emitterPosition + offset * params.size + params.velocity * (hz * params.lifetime);

        const math::Vec4f clip = context.viewProj * math::Vec4f{worldPos, 1.0f};
        if (clip.w <= 1e-3f)
        {
            continue; // Frustum cull particles behind camera.
        }
        const math::Vec3f ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};

        internal::BillboardPush push;
        push.centerSize = {ndc.x, ndc.y, internal::ndcHalfSize(context.viewProj, worldPos, params.size), aspect};
        push.color = params.color;
        push.time = static_cast<float>(i); // Per-particle flicker phase.
        push.intensity = 1.0f;
        push.shape = static_cast<std::uint32_t>(internal::BillboardShape::Disc);
        effect_->render(cmd, &push, sizeof(push));
    }
}

} // namespace starforge::environment
