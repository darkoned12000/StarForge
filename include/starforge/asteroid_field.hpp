#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// Instanced asteroid field (M15.0.8/15.0.9). The facade owns a seeded lumpy
// base mesh and GPU instance storage; a single instanced draw renders the near
// rocks with per-instance tint and procedural shape variety, a distance-aware
// inverted-hull outline keeps silhouettes crisp, and distant rocks switch to a
// cheap impostor draw with no pop at the cull radius.
// Move-only; the renderer passed to create() must outlive the field.
class AsteroidField
{
public:
    struct CreateInfo
    {
        std::string shaderDirectory;
        std::uint32_t variantCount = 4; // seeded variants generated for the field
        std::uint32_t capacity = 4096;  // max instances per frame
    };

    struct RenderContext
    {
        math::Mat4f viewProj{};
        math::Vec2f projectionScale{1.0f, 1.0f}; // proj[0][0], proj[1][1]
        math::Vec2f resolution{1.0f, 1.0f};
        math::Vec3f sunDirection{1.0f, 0.5f, 0.3f};
        math::Vec3f cameraPosition{}; // camera-relative eye position
        float lodDistance = 1200.0f;  // beyond this, rocks use impostors
        float outlineWidthPixels = 1.2f;
        bool outline = true;
    };

    struct Instance
    {
        math::Vec3f position{};    // camera-relative
        float scale = 1.0f;
        math::Vec4f rotation{0.0f, 0.0f, 0.0f, 1.0f}; // unit quaternion xyzw
        math::Vec4f tint{0.55f, 0.45f, 0.35f, 1.0f};  // rgb palette tint
        std::uint32_t seed = 0; // stable per-rock id (shape must not change as you fly)
        // Dissolve factor: 1 = fully visible, 0 = fully dissolved. Lets the
        // caller fade rocks out at the edge of the streamed region so they
        // never pop into or out of existence.
        float fade = 1.0f;
    };

    [[nodiscard]] static std::expected<AsteroidField, std::string> create(Renderer& renderer,
                                                                          const CreateInfo& info);
    ~AsteroidField();
    AsteroidField(AsteroidField&&) noexcept;
    AsteroidField& operator=(AsteroidField&&) noexcept;
    AsteroidField(const AsteroidField&) = delete;
    AsteroidField& operator=(const AsteroidField&) = delete;

    void record(FrameContext& frameContext, const RenderContext& context,
                const Instance* instances, std::uint32_t count);
    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit AsteroidField(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
