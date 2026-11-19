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
