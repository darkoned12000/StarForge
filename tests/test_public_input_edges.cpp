// M16.1 (T1): public input primitives are backend-free and testable.
// Absolute mouse position is a plain value; button edges track level samples.
#include <doctest/doctest.h>

#include "starforge/input.hpp"

using namespace starforge;

TEST_CASE("M16.1: mouse button edges from per-frame level samples")
{
    MouseButtonEdges edges;
    // First frame: all held levels false -> no edges.
    edges.update(false, false, false);
    CHECK_FALSE(edges.pressed(MouseButton::Left));
    CHECK_FALSE(edges.released(MouseButton::Left));

    // Left goes down -> press edge only.
    edges.update(true, false, false);
    CHECK(edges.pressed(MouseButton::Left));
    CHECK_FALSE(edges.released(MouseButton::Left));
    CHECK_FALSE(edges.pressed(MouseButton::Right));

    // Still held -> level persists, no repeat edge.
    edges.update(true, false, false);
    CHECK_FALSE(edges.pressed(MouseButton::Left));
    CHECK_FALSE(edges.released(MouseButton::Left));

    // Left released -> release edge; right pressed in the same frame.
    edges.update(false, true, false);
    CHECK(edges.released(MouseButton::Left));
    CHECK(edges.pressed(MouseButton::Right));
    CHECK_FALSE(edges.pressed(MouseButton::Left));

    // Middle press/release is tracked independently.
    edges.update(false, false, true);
    CHECK(edges.pressed(MouseButton::Middle));
    CHECK(edges.released(MouseButton::Right));
    edges.update(false, false, false);
    CHECK(edges.released(MouseButton::Middle));
}

TEST_CASE("M16.1: MousePosition is a plain value type")
{
    const MousePosition origin{};
    CHECK(origin.x == 0.0f);
    CHECK(origin.y == 0.0f);

    const MousePosition cursor{128.5f, 64.25f};
    CHECK(cursor.x == 128.5f);
    CHECK(cursor.y == 64.25f);
}
