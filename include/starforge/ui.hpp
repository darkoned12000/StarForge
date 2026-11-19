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

    [[nodiscard]] static std::expected<UiCanvas, std::string> create(Renderer& renderer,
                                                                     const CreateInfo& info);
    ~UiCanvas();
    UiCanvas(UiCanvas&&) noexcept;
    UiCanvas& operator=(UiCanvas&&) noexcept;
    UiCanvas(const UiCanvas&) = delete;
    UiCanvas& operator=(const UiCanvas&) = delete;

    void clear() noexcept;
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
