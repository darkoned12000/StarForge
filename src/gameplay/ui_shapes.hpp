#pragma once

// Pure-CPU geometry for toon UI widgets. No Vulkan/math/backend types.
// ColoredQuad::colorIndex maps to the caller's palette slot.
#include "gameplay/ui_relation.hpp"
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

// M17.5a: relation markers. Every relation has a distinct silhouette so
// friend/foe does not rely on colour alone. `half` is the marker radius in
// pixels and `converge` (0..1) closes the brackets during lock-on.
[[nodiscard]] inline std::vector<ColoredQuad>
relationMarkerQuads(ui_relation::Shape shape, float cx, float cy, float half, float converge = 1.0f)
{
    std::vector<ColoredQuad> out;
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(half) || half <= 0.0f)
    {
        return out;
    }
    const float c = std::clamp(converge, 0.0f, 1.0f);
    // Brackets start wide and close in over the lock-on window.
    const float r = half * (1.0f + (1.0f - c) * 0.8f);
    const float arm = std::max(2.0f, half * 0.42f);
    const float t = std::max(1.0f, half * 0.07f);

    auto line = [&](float x0, float y0, float x1, float y1) {
        out.push_back({{x0, y0, x1, y1}, 0});
    };

    if (shape == ui_relation::Shape::Diamond)
    {
        // Four pointed chevrons (N/E/S/W), stepped with axis-aligned rects.
        const float s = r * 0.45f;
        line(cx - s, cy - r, cx + s, cy - r);
        line(cx - t, cy - r, cx + t, cy - r + arm);
        line(cx - s, cy + r, cx + s, cy + r);
        line(cx - t, cy + r - arm, cx + t, cy + r);
        line(cx - r, cy - s, cx - r, cy + s);
        line(cx - r + arm, cy - t, cx - r, cy + t);
        line(cx + r, cy - s, cx + r, cy + s);
        line(cx + r, cy - t, cx + r - arm, cy + t);
    }
    else if (shape == ui_relation::Shape::Rounded)
    {
        // Short corner strokes plus softened corner dots.
        const float inset = arm * 0.55f;
        for (const float sx : {-1.0f, 1.0f})
        {
            for (const float sy : {-1.0f, 1.0f})
            {
                const float x = cx + sx * r;
                const float y = cy + sy * r;
                line(x - (sx > 0.0f ? arm : 0.0f), y - t,
                     x + (sx < 0.0f ? arm : 0.0f), y + t);
                line(x - t, y - (sy > 0.0f ? arm : 0.0f),
                     x + t, y + (sy < 0.0f ? arm : 0.0f));
                line(x - inset, y - inset, x + inset, y + inset);
            }
        }
    }
    else
    {
        // Square corner brackets (neutral + self).
        for (const float sx : {-1.0f, 1.0f})
        {
            for (const float sy : {-1.0f, 1.0f})
            {
                const float x = cx + sx * r;
                const float y = cy + sy * r;
                line(x - (sx > 0.0f ? arm : 0.0f), y - t,
                     x + (sx < 0.0f ? arm : 0.0f), y + t);
                line(x - t, y - (sy > 0.0f ? arm : 0.0f),
                     x + t, y + (sy < 0.0f ? arm : 0.0f));
            }
        }
    }
    return out;
}

} // namespace starforge::gameplay::ui_shapes
