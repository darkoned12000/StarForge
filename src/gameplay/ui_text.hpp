#pragma once

// Built-in 5x7 bitmap font for ASCII 32..126 (pure CPU, no assets).
// Each glyph is 7 rows of 5 bits, MSB = leftmost pixel.
#include <cstddef>
#include <string_view>
#include <vector>

namespace starforge::gameplay::ui_text
{

inline constexpr std::size_t kGlyphWidth = 5;
inline constexpr std::size_t kGlyphHeight = 7;
inline constexpr std::size_t kGlyphCount = 95;

struct Glyph { unsigned char rows[kGlyphHeight] = {}; };

inline constexpr Glyph kGlyphs[kGlyphCount] = {
    {0,0,0,0,0,0,0},
    {4,4,4,4,4,0,4},
    {10,10,0,0,0,0,0},
    {10,10,31,10,31,10,10},
    {4,15,20,14,5,30,4},
    {25,26,2,4,8,19,19},
    {12,18,20,8,21,18,13},
    {4,4,0,0,0,0,0},
    {2,4,8,8,8,4,2},
    {8,4,2,2,2,4,8},
    {0,4,21,14,21,4,0},
    {0,4,4,31,4,4,0},
    {0,0,0,0,4,4,8},
    {0,0,0,31,0,0,0},
    {0,0,0,0,0,4,4},
    {1,1,2,4,8,16,16},
    {14,17,19,21,25,17,14},
    {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},
    {31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14},
    {31,1,2,4,4,4,4},
    {14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12},
    {0,4,4,0,4,4,0},
    {0,4,4,0,4,4,8},
    {2,4,8,16,8,4,2},
    {0,0,31,0,31,0,0},
    {8,4,2,1,2,4,8},
    {14,17,1,2,4,0,4},
    {14,17,23,21,23,16,14},
    {4,10,17,17,31,17,17},
    {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14},
    {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},
    {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},
    {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},
    {17,17,17,21,21,27,17},
    {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},
    {31,1,2,4,8,16,31},
    {14,8,8,8,8,8,14},
    {16,16,8,4,2,1,1},
    {14,2,2,2,2,2,14},
    {4,10,17,0,0,0,0},
    {0,0,0,0,0,0,31},
    {8,4,0,0,0,0,0},
    {0,0,14,1,15,17,15},
    {16,16,30,17,17,17,30},
    {0,0,15,16,16,16,15},
    {1,1,15,17,17,17,15},
    {0,0,14,17,31,16,14},
    {6,8,16,30,16,16,16},
    {0,15,17,17,15,1,14},
    {16,16,30,17,17,17,17},
    {4,0,12,4,4,4,14},
    {2,0,6,2,2,18,12},
    {16,16,20,20,24,20,20},
    {12,4,4,4,4,4,14},
    {0,0,26,21,21,21,21},
    {0,0,30,17,17,17,17},
    {0,0,14,17,17,17,14},
    {0,30,17,17,30,16,16},
    {0,15,17,17,15,1,1},
    {0,0,22,24,16,16,16},
    {0,0,15,16,14,1,30},
    {8,8,30,8,8,8,6},
    {0,0,17,17,17,17,15},
    {0,0,17,17,17,10,4},
    {0,0,17,17,21,21,10},
    {0,0,17,10,4,10,17},
    {0,17,17,17,15,1,14},
    {0,0,31,2,4,8,31},
    {2,4,4,8,4,4,2},
    {4,4,4,4,4,4,4},
    {8,4,4,2,4,4,8},
    {0,0,6,9,0,0,0},
};

[[nodiscard]] inline const Glyph& glyph(char c) noexcept
{
    const unsigned char i = static_cast<unsigned char>(c);
    return kGlyphs[(i >= 32 && i <= 126) ? i - 32 : 0];
}

// Advance width in glyph cells (1 per char + 1 space).
[[nodiscard]] inline std::size_t textCells(std::string_view text) noexcept
{
    return text.empty() ? 0 : text.size() * 2 - 1;
}

// One axis-aligned quad per lit font pixel, in window coordinates
// (top-left origin). Pure CPU so T1/T3 share the exact glyph path.
struct Quad
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

[[nodiscard]] inline std::vector<Quad> textQuads(std::string_view text, float x, float y, float scale)
{
    std::vector<Quad> quads;
    for (std::size_t ci = 0; ci < text.size(); ++ci)
    {
        const auto& g = glyph(text[ci]);
        const float originX = x + static_cast<float>(ci * (kGlyphWidth + 1)) * scale;
        for (std::size_t row = 0; row < kGlyphHeight; ++row)
        {
            for (std::size_t col = 0; col < kGlyphWidth; ++col)
            {
                if (((g.rows[row] >> (kGlyphWidth - 1 - col)) & 1u) == 0) continue;
                const float x0 = originX + static_cast<float>(col) * scale;
                const float y0 = y + static_cast<float>(row) * scale;
                quads.push_back(Quad{x0, y0, x0 + scale, y0 + scale});
            }
        }
    }
    return quads;
}

} // namespace starforge::gameplay::ui_text
