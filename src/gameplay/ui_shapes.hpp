#pragma once

// Pure-CPU geometry for toon UI widgets. No Vulkan/math/backend types.
// ColoredQuad::colorIndex maps to the caller's palette slot.
#include "gameplay/ui_text.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace starforge::gameplay::ui_shapes
{

struct ColoredQuad
{
    ui_text::Quad quad{};
    std::size_t colorIndex = 0; // 0 = fill, 1 = outline, 2 = accent
};

// Outlined flat panel: outer border, inset fill, thin accent strip at top.
[[nodiscard]] inline std::vector<ColoredQuad>
panelQuads(float x, float y, float w, float h, float outlineWidth)
{
    std::vector<ColoredQuad> out;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w <= 0.0f ||
        h <= 0.0f)
    {
        return out;
    }
    const float safeOutline = std::isfinite(outlineWidth) ? outlineWidth : 0.0f;
    const float maxOutline = std::max(0.0f, std::min(w, h) * 0.25f);
    const float ow = std::clamp(safeOutline, 0.0f, maxOutline);
    out.push_back({{x, y, x + w, y + h}, 1}); // outline
    out.push_back({{x + ow, y + ow, x + w - ow, y + h - ow}, 0}); // fill
    // Thin top strip so it never collides with panel titles/text.
    const float accentH = std::min(2.0f, h * 0.04f);
    out.push_back({{x + ow, y + ow, x + w - ow, y + ow + accentH}, 2}); // rim accent
    return out;
}

// Progress bar: dark track + value-driven fill (value clamped to 0..1).
[[nodiscard]] inline std::vector<ColoredQuad>
barQuads(float x, float y, float w, float h, float value)
{
    std::vector<ColoredQuad> out;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w <= 0.0f ||
        h <= 0.0f)
    {
        return out;
    }
    out.push_back({{x, y, x + w, y + h}, 1}); // track
    const float safeValue = std::isfinite(value) ? value : 0.0f;
    const float v = std::clamp(safeValue, 0.0f, 1.0f);
    out.push_back({{x, y, x + w * v, y + h}, 0}); // fill
    return out;
}

} // namespace starforge::gameplay::ui_shapes
