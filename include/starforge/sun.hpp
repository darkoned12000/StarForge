#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"
#include "starforge/world.hpp"

#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// Public sun facade (M15.0.7): Renders a visitable 3D celestial sun with:
// - Real 3D geometry via cubed-sphere patch LOD (LargeObjectSetup)
// - Opaque core with Eddington limb darkening (1 - u(1 - μ))
// - Additive solar corona shell (5-10x radius) occluded behind the opaque disc
// Move-only; the renderer passed to create() must outlive the sun.
class Sun
{
public:
    struct CreateInfo
    {
        // Directory containing the bundled sun shaders (sun.vert/frag.spv, sun_corona.vert/frag.spv).
        // Empty resolves relative to the running executable.
        std::string shaderDirectory;
        double defaultRadius = 10000.0;
        std::uint32_t subdivisions = 16;
    };

    struct RenderContext
    {
        math::Mat4f viewProj{};
        WorldPosition cameraPosition{};
        math::Vec2f resolution{0.0f, 0.0f};
        float time = 0.0f;
    };

    struct Params
    {
        WorldPosition worldPosition{};
        double radius = 10000.0;                          // Photosphere radius in meters
        double coronaRadiusMultiplier = 6.0;             // Outer corona radius (5x - 10x)
        math::Vec4f coreColor{1.0f, 0.95f, 0.82f, 1.0f};  // Bright disk emission
        math::Vec4f limbColor{0.95f, 0.38f, 0.05f, 1.0f}; // Limb darkening rim color
        math::Vec4f coronaColor{1.0f, 0.65f, 0.20f, 1.0f};// Corona atmospheric glow
        float limbDarkeningCoeff = 0.6f;                  // Eddington factor u in 1 - u(1 - μ)
        float coronaIntensity = 1.0f;
        float time = 0.0f;
    };

    [[nodiscard]] static std::expected<Sun, std::string> create(Renderer& renderer, const CreateInfo& info);

    ~Sun();
    Sun(Sun&& other) noexcept;
    Sun& operator=(Sun&& other) noexcept;
    Sun(const Sun&) = delete;
    Sun& operator=(const Sun&) = delete;

    // Records the sun into an active dynamic rendering frame context.
    void record(FrameContext& frameContext, const RenderContext& context, const Params& params);

    // Standalone helper: clears, records the sun, and presents.
    [[nodiscard]] std::expected<Renderer::FrameResult, std::string>
    render(Renderer& renderer, const RenderContext& context, const Params& params);

    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit Sun(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
