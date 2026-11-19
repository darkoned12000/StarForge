// M16.2 (T1): the built-in bitmap font is deterministic and asset-free.
#include <doctest/doctest.h>

#include "gameplay/ui_text.hpp"

using namespace starforge::gameplay::ui_text;

TEST_CASE("M16.2: bitmap font covers ASCII 32..126")
{
    CHECK(kGlyphWidth == 5);
    CHECK(kGlyphHeight == 7);
    CHECK(kGlyphCount == 95);

    const Glyph& space = glyph(' ');
    for (unsigned char row : space.rows) CHECK(row == 0);

    // Out-of-range characters map to the blank glyph.
    CHECK(glyph('\0').rows[0] == 0);
    CHECK(glyph('\x7f').rows[0] == 0);

    // Every glyph row uses only the low 5 bits.
    for (std::size_t i = 0; i < kGlyphCount; ++i)
        for (std::size_t r = 0; r < kGlyphHeight; ++r)
            CHECK((kGlyphs[i].rows[r] & 0xE0u) == 0);
}

TEST_CASE("M16.2: glyph bit patterns and text metrics")
{
    const Glyph& a = glyph('A');
    // 'A': ..#.. / .#.#. / #...# / #...# / ##### / #...# / #...#
    CHECK(a.rows[0] == 0b00100);
    CHECK(a.rows[1] == 0b01010);
    CHECK(a.rows[2] == 0b10001);
    CHECK(a.rows[4] == 0b11111);
    CHECK(a.rows[6] == 0b10001);

    const Glyph& zero = glyph('0');
    CHECK(zero.rows[0] == 0b01110);
    CHECK(zero.rows[6] == 0b01110);

    CHECK(textCells("") == 0);
    CHECK(textCells("A") == 1);
    CHECK(textCells("ABC") == 5);
    CHECK(textCells("HELLO") == 9);
}

#include "gameplay/ui_shapes.hpp"

TEST_CASE("M16.3: toon widget geometry is deterministic")
{
    using namespace starforge::gameplay::ui_shapes;

    const auto panel = panelQuads(10.0f, 20.0f, 100.0f, 50.0f, 2.0f);
    REQUIRE(panel.size() == 3);
    CHECK(panel[0].colorIndex == 1); // outline
    CHECK(panel[0].quad.x0 == 10.0f);
    CHECK(panel[0].quad.x1 == 110.0f);
    CHECK(panel[1].colorIndex == 0); // inset fill
    CHECK(panel[1].quad.x0 == 12.0f);
    CHECK(panel[1].quad.x1 == 108.0f);
    CHECK(panel[2].colorIndex == 2); // rim accent
    CHECK(panel[2].quad.y0 == 22.0f); // inset by outline width from panel y=20
    CHECK(panel[2].quad.y1 == 24.0f); // thin 2 px strip so it clears titles

    const auto bar = barQuads(0.0f, 0.0f, 100.0f, 10.0f, 0.25f);
    REQUIRE(bar.size() == 2);
    CHECK(bar[0].colorIndex == 1); // track
    CHECK(bar[1].colorIndex == 0); // fill
    CHECK(bar[1].quad.x0 == 0.0f);
    CHECK(bar[1].quad.x1 == 25.0f);

    const auto empty = barQuads(0.0f, 0.0f, 100.0f, 10.0f, -1.0f);
    CHECK(empty[1].quad.x1 == 0.0f); // clamped to zero
    const auto full = barQuads(0.0f, 0.0f, 100.0f, 10.0f, 5.0f);
    CHECK(full[1].quad.x1 == 100.0f); // clamped to full
}
