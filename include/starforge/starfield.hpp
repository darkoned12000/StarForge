#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// Public starfield facade: procedural starfield plus sun billboard, proven by
// the sandbox working slice. Owns its pipeline, shader modules, and bundled
// shader artifacts; callers supply only scene-level parameters (view matrix,
// time, intensity, color, radius). Move-only; the renderer passed to create()
// must outlive the starfield. Call Renderer::waitIdle() before destroying
// or move-assigning a starfield that has recorded frames.
class Starfield
{
public:
    struct CreateInfo
    {
        // Directory containing the bundled starfield shaders. Empty resolves
        // the bundled artifacts relative to the running executable, falling
        // back to the build shader directory.
        std::string shaderDirectory;
    };

    struct RenderContext
    {
        math::Mat4f viewProj{};
        math::Mat4f invViewProj{1.0f};
        math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
        // Target resolution in pixels. Zero uses the frame extent.
        math::Vec2f resolution{0.0f, 0.0f};
        float time = 0.0f;
        Renderer::ClearColor clearColor{};
    };

    struct Params
    {
        math::Vec3f worldPosition{0.0f, 0.0f, 0.0f};
        float radius = 1.0f;
        float intensity = 1.0f;
        math::Vec4f color{1.0f, 0.9f, 0.7f, 1.0f};
        float limbDarkeningCoeff = 0.3f;
    };

    [[nodiscard]] static std::expected<Starfield, std::string> create(Renderer& renderer, const CreateInfo& info);

    ~Starfield();
    Starfield(Starfield&& other) noexcept;
    Starfield& operator=(Starfield&& other) noexcept;
    Starfield(const Starfield&) = delete;
    Starfield& operator=(const Starfield&) = delete;

    // Records the starfield into an active frame (inside
    // Renderer::renderFrame). Multiple objects can record into the same
    // frame this way before a single presentation.
    void record(FrameContext& frameContext, const RenderContext& context, const Params& params);

    // Standalone helper: clears, records the starfield, and presents.
    [[nodiscard]] std::expected<Renderer::FrameResult, std::string>
    render(Renderer& renderer, const RenderContext& context, const Params& params);

    // Recreates the pipeline for the renderer's current color format after a
    // swapchain recreation that changed formats (see Renderer::recreate()).
    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit Starfield(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
