#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// Toon HUD facade: outlined flat panels, a portrait frame and graphic accent
// bars, styled consistently with the toon material (dark graphic outlines,
// flat colors, warm rim accents). Records into an active frame.
class Hud
{
public:
    struct CreateInfo
    {
        std::string shaderDirectory;
    };

    struct Params
    {
        float panelX = 8.0f, panelY = 8.0f;
        float panelWidth = 112.0f, panelHeight = 48.0f;
        float portraitX = 14.0f, portraitY = 14.0f;
        float portraitWidth = 36.0f, portraitHeight = 36.0f;
        math::Vec4f panelColor{0.08f, 0.12f, 0.22f, 1.0f};
        math::Vec4f outlineColor{0.02f, 0.02f, 0.04f, 1.0f};
        math::Vec4f accentColor{0.95f, 0.65f, 0.15f, 1.0f};
        float barValues[3] = {1.0f, 0.75f, 0.5f}; // normalized readout bar lengths
    };

    static std::expected<Hud, std::string> create(Renderer& renderer, const CreateInfo& info);

    ~Hud();
    Hud(Hud&&) noexcept;
    Hud& operator=(Hud&&) noexcept;
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    void record(FrameContext& frameContext, const Params& params);
    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit Hud(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
