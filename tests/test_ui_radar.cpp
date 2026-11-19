// M16.6 (T1): radar projection and contact filtering are pure CPU.
#include <doctest/doctest.h>

#include "ui_radar.hpp"

using namespace starforge::game;

TEST_CASE("M16.6: radar projects ahead/right and culls beyond range")
{
    constexpr float range = 200.0f;
    constexpr float radius = 50.0f;

    const auto centre = projectRadar({"self", 0.0f, 0.0f, RadarKind::Rock}, range, radius);
    CHECK(centre.visible);
    CHECK(centre.x == doctest::Approx(0.0f));
    CHECK(centre.y == doctest::Approx(0.0f));
    CHECK(centre.distance == doctest::Approx(0.0f));

    const auto ahead = projectRadar({"planet", 0.0f, 100.0f, RadarKind::Planet}, range, radius);
    CHECK(ahead.visible);
    CHECK(ahead.x == doctest::Approx(0.0f));
    CHECK(ahead.y == doctest::Approx(-25.0f)); // top half
    CHECK(ahead.bearing == doctest::Approx(0.0f));

    const auto right = projectRadar({"hostile", 100.0f, 0.0f, RadarKind::Hostile}, range, radius);
    CHECK(right.visible);
    CHECK(right.x == doctest::Approx(25.0f));
    CHECK(right.y == doctest::Approx(0.0f));
    CHECK(right.bearing == doctest::Approx(90.0f));

    const auto behind = projectRadar({"rock", 0.0f, -100.0f, RadarKind::Rock}, range, radius);
    CHECK(behind.visible);
    CHECK(behind.y == doctest::Approx(25.0f)); // bottom half
    CHECK(behind.bearing == doctest::Approx(180.0f));

    const auto far = projectRadar({"far", 0.0f, 201.0f, RadarKind::Station}, range, radius);
    CHECK_FALSE(far.visible);
    CHECK(far.distance == doctest::Approx(201.0f));
}

TEST_CASE("M16.6: radar handles degenerate ranges")
{
    const auto p = projectRadar({"c", 10.0f, 10.0f, RadarKind::Rock}, 0.0f, 50.0f);
    CHECK_FALSE(p.visible);
    const auto q = projectRadar({"c", 10.0f, 10.0f, RadarKind::Rock}, 100.0f, 0.0f);
    CHECK_FALSE(q.visible);
}
