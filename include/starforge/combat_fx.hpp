#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// M17.5 transient combat effects. Effects are produced by gameplay events,
// live only for a few frames and are never persisted. The facade owns one
// fixed-capacity GPU buffer and issues a single instanced additive draw into
// the HDR scene target.
class CombatFx
{
public:
    struct CreateInfo
    {
        std::string shaderDirectory;
        // Total simultaneous instances across all effect kinds. Enough for
        // 256 bolts, 2048 particles and 8 layered explosions.
        std::uint32_t capacity = 4096;
    };

    enum class Kind : std::uint32_t
    {
        Beam = 0,     // bolt or debris trail
        Spark = 1,    // impact spark / smoke puff
        Ring = 2,     // shockwave
        Fireball = 3, // explosion body
        Plume = 4,    // engine exhaust
    };

    struct Instance
    {
        math::Vec3f position{};  // camera-relative world metres
        float radius = 1.0f;
        math::Vec3f direction{}; // beam/plume axis; zero for billboards
        Kind kind = Kind::Spark;
        math::Vec4f color{};     // rgb HDR colour (>1 blooms), a intensity
        float age = 0.0f;
        float seed = 0.0f;
        float aspect = 1.0f;
        float fade = 1.0f;
    };

    struct RenderContext
    {
        math::Mat4f viewProj{};
        math::Vec4f cameraRight{};
        math::Vec4f cameraUp{};
        math::Vec2f resolution{1.0f, 1.0f};
        math::Vec3f cameraPosition{}; // camera-relative eye (for safe billboards)
    };

    [[nodiscard]] static std::expected<CombatFx, std::string>
    create(Renderer& renderer, const CreateInfo& info);
    ~CombatFx();
    CombatFx(CombatFx&&) noexcept;
    CombatFx& operator=(CombatFx&&) noexcept;
    CombatFx(const CombatFx&) = delete;
    CombatFx& operator=(const CombatFx&) = delete;

    // Count is clamped to the capacity at creation; no allocation happens
    // on the render path and no buffer a frame in flight may read is freed.
    void record(FrameContext& frameContext, const RenderContext& context,
                const Instance* instances, std::uint32_t count);
    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit CombatFx(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
