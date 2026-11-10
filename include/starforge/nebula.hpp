#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// Public nebula facade: volumetric-look FBM backdrop. Owns its
// pipeline and bundled shaders; callers supply only scene-level
// parameters (view matrices, color, density). Move-only; the
// renderer passed to create() must outlive the nebula.
class Nebula
{
public:
    struct CreateInfo
    {
        std::string shaderDirectory;
    };

    struct RenderContext
    {
        math::Mat4f viewProj{};
        math::Mat4f invViewProj{1.0f};
        math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
        math::Vec2f resolution{0.0f, 0.0f};
        float time = 0.0f;
    };

    struct Params
    {
        math::Vec4f baseColor{0.5f, 0.2f, 0.8f, 1.0f};
        float density = 1.0f;
        float absorption = 0.1f;
        float scattering = 0.5f;
        float time = 0.0f;
    };

    [[nodiscard]] static std::expected<Nebula, std::string> create(Renderer& renderer, const CreateInfo& info);

    ~Nebula();
    Nebula(Nebula&& other) noexcept;
    Nebula& operator=(Nebula&& other) noexcept;
    Nebula(const Nebula&) = delete;
    Nebula& operator=(const Nebula&) = delete;

    void record(FrameContext& frameContext, const RenderContext& context, const Params& params);

    [[nodiscard]] std::expected<Renderer::FrameResult, std::string>
    render(Renderer& renderer, const RenderContext& context, const Params& params);

    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit Nebula(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
