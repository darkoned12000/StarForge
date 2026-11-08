#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// Public toon (cel-shaded) facade: quantized diffuse bands, stepped
// specular, rim light, and an inverted-hull outline pass. Owns its
// pipelines and bundled shader artifacts; callers supply only
// scene-level parameters (view matrices, light direction, colors,
// band count, rim strength, outline thickness in pixels). Move-only;
// the renderer passed to create() must outlive the toon renderer.
// Call Renderer::waitIdle() before destroying or move-assigning a
// toon renderer that has recorded frames.
class Toon
{
public:
    struct CreateInfo
    {
        // Directory containing the bundled toon shaders. Empty loads
        // the bundled artifacts from the build's shader directory.
        std::string shaderDirectory;
    };

    struct Context
    {
        // Clip matrix (projection * modelView) and the modelView matrix
        // (view space: camera at origin looking down -Z).
        math::Mat4f viewProj{};
        math::Mat4f modelView{1.0f};
        // Direction toward the light, in view space.
        math::Vec3f lightDirectionView{0.0f, 0.0f, 1.0f};
        // Target resolution in pixels; keeps outline thickness constant
        // in screen space. Zero falls back to the frame extent.
        math::Vec2f resolution{0.0f, 0.0f};
    };

    struct Params
    {
        math::Vec4f baseColor{1.0f, 0.25f, 0.2f, 1.0f};
        math::Vec4f shadowColor{0.25f, 0.05f, 0.1f, 1.0f};
        float bandCount = 3.0f;
        float rimStrength = 0.5f;
        float outlineWidth = 2.0f; // pixels
    };

    [[nodiscard]] static std::expected<Toon, std::string> create(Renderer& renderer, const CreateInfo& info);

    ~Toon();
    Toon(Toon&& other) noexcept;
    Toon& operator=(Toon&& other) noexcept;
    Toon(const Toon&) = delete;
    Toon& operator=(const Toon&) = delete;

    // Records the toon mesh (base pass, then outline pass) into an
    // active frame (inside Renderer::renderFrame).
    void record(FrameContext& frameContext, const Context& context, const Params& params);

    // Standalone helper: clears, records the toon mesh, and presents.
    [[nodiscard]] std::expected<Renderer::FrameResult, std::string>
    render(Renderer& renderer, const Context& context, const Params& params);

    // Recreates the pipelines for the renderer's current color format
    // after a swapchain recreation that changed formats.
    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit Toon(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
