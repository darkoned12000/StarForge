#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace starforge
{

// Retained-mode toon UI canvas (M16.2/M16.3). Build text draws each frame,
// then record them in one public-API call. Coordinates are window pixels
// with a top-left origin; `scale` is the size of one font pixel.
// Move-only; the renderer passed to create() must outlive the canvas.
class UiCanvas
{
public:
    struct CreateInfo
    {
        std::string shaderDirectory;
    };

    struct TextDraw
    {
        float x = 0.0f;
        float y = 0.0f;
        float scale = 2.0f;
        math::Vec4f color{1.0f, 1.0f, 1.0f, 1.0f};
        std::string text;
    };

    // Flat rectangle in window-pixel coordinates.
    struct RectDraw
    {
        float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;
        math::Vec4f color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    // Outlined toon panel: border, inset fill, thin rim accent.
    struct PanelDraw
    {
        float x = 0.0f, y = 0.0f, w = 100.0f, h = 50.0f;
        float outlineWidth = 2.0f;
        math::Vec4f fillColor{0.08f, 0.12f, 0.22f, 1.0f};
        math::Vec4f outlineColor{0.02f, 0.02f, 0.04f, 1.0f};
        math::Vec4f accentColor{0.95f, 0.65f, 0.15f, 1.0f};
    };

    // Progress bar: track + value-driven fill.
    struct BarDraw
    {
        float x = 0.0f, y = 0.0f, w = 100.0f, h = 8.0f;
        float value = 1.0f;
        math::Vec4f fillColor{0.2f, 0.8f, 0.4f, 1.0f};
        math::Vec4f trackColor{0.05f, 0.05f, 0.1f, 1.0f};
    };

    [[nodiscard]] static std::expected<UiCanvas, std::string> create(Renderer& renderer,
                                                                     const CreateInfo& info);
    ~UiCanvas();
    UiCanvas(UiCanvas&&) noexcept;
    UiCanvas& operator=(UiCanvas&&) noexcept;
    UiCanvas(const UiCanvas&) = delete;
    UiCanvas& operator=(const UiCanvas&) = delete;

    void clear() noexcept;
    // Widgets are queued in insertion order; backgrounds should be added
    // before text so text composites on top.
    void addRect(const RectDraw& draw);
    void addPanel(const PanelDraw& draw);
    void addBar(const BarDraw& draw);
    void addText(const TextDraw& draw);
    void record(FrameContext& frameContext);

    // Total quads/vertices produced by the current draws (T1/T3 proof hook).
    [[nodiscard]] std::size_t vertexCount() const noexcept;

private:
    struct Impl;
    explicit UiCanvas(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
