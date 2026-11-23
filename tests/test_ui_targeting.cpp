#include <doctest/doctest.h>

#include "gameplay/ui_relation.hpp"
#include "gameplay/ui_targeting.hpp"

#include <cmath>

using namespace starforge::gameplay;

TEST_CASE("ui_targeting: relation colour and shape are consistent")
{
    // Colour alone must not carry the meaning; each relation also has a
    // distinct silhouette used by markers, radar and the target panel.
    CHECK(ui_relation::shape(ui_relation::Relation::Ally) == ui_relation::Shape::Rounded);
    CHECK(ui_relation::shape(ui_relation::Relation::Neutral) == ui_relation::Shape::Square);
    CHECK(ui_relation::shape(ui_relation::Relation::Hostile) == ui_relation::Shape::Diamond);

    const auto ally = ui_relation::color(ui_relation::Relation::Ally);
    const auto hostile = ui_relation::color(ui_relation::Relation::Hostile);
    const auto neutral = ui_relation::color(ui_relation::Relation::Neutral);
    // Cyan vs red-orange: blue channel separates ally from hostile.
    CHECK(ally.b > ally.r);
    CHECK(hostile.r > hostile.g);
    CHECK(neutral.r > neutral.b);
}

TEST_CASE("ui_targeting: lead solver intercepts a crossing target")
{
    // Target 100 m to the left, moving across at 50 m/s, bolt at 600 m/s.
    const auto result = ui_targeting::solveLead({-100.0f, 0.0f, 0.0f},
                                                {0.0f, 0.0f, 50.0f}, 600.0f);
    REQUIRE(result.valid);
    CHECK(result.time > 0.0f);
    // The lead point must be ahead of the target along its velocity.
    CHECK(result.aim.z > 0.0f);
    // |p + v*t| == speed*t at the solution.
    const float distance = ui_targeting::length(result.aim);
    CHECK(distance == doctest::Approx(600.0f * result.time).epsilon(1e-3f));
}

TEST_CASE("ui_targeting: lead solver rejects impossible and degenerate shots")
{
    // Target moving away faster than the bolt cannot be led.
    CHECK_FALSE(ui_targeting::solveLead({0.0f, 0.0f, -10.0f},
                                        {0.0f, 0.0f, -900.0f}, 600.0f).valid);
    // Zero bolt speed and stationary target are invalid.
    CHECK_FALSE(ui_targeting::solveLead({0.0f, 0.0f, -10.0f}, {}, 0.0f).valid);
    CHECK_FALSE(ui_targeting::solveLead({}, {}, 600.0f).valid);

    // A target closing at exactly bolt speed still has a finite intercept.
    const auto equal = ui_targeting::solveLead({0.0f, 0.0f, -100.0f},
                                               {0.0f, 0.0f, 600.0f}, 600.0f);
    CHECK(equal.valid);
}

TEST_CASE("ui_targeting: edge arrow clamps to the screen and flips behind")
{
    const float w = 1920.0f;
    const float h = 1080.0f;
    const float inset = 48.0f;

    const auto ahead = ui_targeting::edgeArrow({0.6f, 0.0f, -1.0f}, 0.2f, 0.0f, w, h, inset);
    CHECK(ahead.onScreen);
    CHECK_FALSE(ahead.behind);

    const auto right = ui_targeting::edgeArrow({1.0f, 0.0f, -1.0f}, 2.0f, 0.0f, w, h, inset);
    CHECK_FALSE(right.onScreen);
    CHECK(right.x == doctest::Approx(w - inset));
    CHECK(right.y == doctest::Approx(h * 0.5f));

    const auto behind = ui_targeting::edgeArrow({0.0f, 0.0f, 1.0f}, 0.0f, 0.0f, w, h, inset);
    CHECK(behind.behind);
    CHECK_FALSE(behind.onScreen);
    // Behind targets flip the panel direction so the arrow points the short
    // way round (up when there is no lateral offset).
    CHECK(behind.y == doctest::Approx(inset));
}

TEST_CASE("ui_targeting: turn cue fades as the target lines up")
{
    const auto lined = ui_targeting::turnCue({0.0f, 0.0f, -1.0f});
    CHECK_FALSE(lined.visible);

    const auto offset = ui_targeting::turnCue({0.4f, 0.0f, -1.0f});
    CHECK(offset.visible);
    CHECK(offset.strength > 0.0f);
    CHECK(offset.strength <= 1.0f);
    CHECK(offset.angle == doctest::Approx(0.0f)); // to the right

    const auto behind = ui_targeting::turnCue({0.0f, 0.0f, 1.0f});
    CHECK(behind.visible);
}
