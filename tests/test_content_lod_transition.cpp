#include <doctest/doctest.h>

#include "content/lod_transition.hpp"

using namespace starforge::content;

namespace
{

[[nodiscard]] ToonLodHysteresis makeSelector()
{
    return ToonLodHysteresis({100.0, 200.0}, 20.0);
}

} // namespace

TEST_CASE("Toon LOD: stable fields draw their own outline")
{
    auto selector = makeSelector();
    const auto near = selector.update(50.0);
    CHECK(near.level == 0);
    CHECK(!near.fading);
    CHECK(near.incomingOutlineEnabled);
    CHECK(near.amount == doctest::Approx(0.0));

    const auto far = selector.update(300.0);
    CHECK(far.level == 2);
    CHECK(!far.fading);
    CHECK(far.incomingOutlineEnabled);
}

TEST_CASE("Toon LOD: crossfade arms inside the band and commits past it")
{
    auto selector = makeSelector();
    (void)selector.update(50.0);

    const auto entering = selector.update(95.0); // Band [90, 110].
    CHECK(entering.fading);
    CHECK(entering.fromLevel == 0);
    CHECK(entering.toLevel == 1);
    CHECK(entering.level == 1);
    CHECK(entering.outlineLevel() == 0);
    CHECK(!entering.incomingOutlineEnabled);
    CHECK(entering.amount == doctest::Approx(0.25));

    const auto midBand = selector.update(100.0);
    CHECK(midBand.fading);
    CHECK(midBand.outlineLevel() == 1);
    CHECK(midBand.amount == doctest::Approx(0.5));

    const auto committed = selector.update(115.0);
    CHECK(!committed.fading);
    CHECK(committed.level == 1);
    CHECK(committed.incomingOutlineEnabled);

    const auto still = selector.update(150.0);
    CHECK(!still.fading);
    CHECK(still.level == 1);
}

TEST_CASE("Toon LOD: reversing out of the band cancels the transition")
{
    auto selector = makeSelector();
    (void)selector.update(50.0);
    (void)selector.update(95.0);
    (void)selector.update(100.0);

    const auto reversed = selector.update(80.0);
    CHECK(!reversed.fading);
    CHECK(reversed.level == 0);
    CHECK(reversed.incomingOutlineEnabled);
}

TEST_CASE("Toon LOD: returning from the coarse level re-enters the fine level")
{
    auto selector = makeSelector();
    (void)selector.update(115.0);
    REQUIRE(selector.level() == 1);

    const auto entering = selector.update(105.0);
    CHECK(entering.fading);
    CHECK(entering.fromLevel == 1);
    CHECK(entering.toLevel == 0);
    CHECK(!entering.incomingOutlineEnabled);

    const auto committed = selector.update(85.0);
    CHECK(!committed.fading);
    CHECK(committed.level == 0);
}

TEST_CASE("Toon LOD: distance jitter cannot oscillate the level")
{
    auto selector = makeSelector();
    REQUIRE(selector.update(115.0).level == 1);

    // Inside the band the selector begins a fine-level fade rather than
    // snapping back instantly.
    const auto entering = selector.update(100.0);
    REQUIRE(entering.fading);
    REQUIRE(entering.toLevel == 0);

    // Repeated identical updates inside the band keep the incoming
    // level stable, and the blend follows the camera.
    const auto first = selector.update(100.0);
    const auto second = selector.update(100.0);
    CHECK(first.level == 0);
    CHECK(second.level == 0);
    CHECK(second.amount == doctest::Approx(first.amount));
}

TEST_CASE("Toon LOD: overlapping threshold bands are clamped, not fought over")
{
    // Bands of width 20 around thresholds 100 and 110 would overlap;
    // the constructor caps the band to the 10 m spacing.
    ToonLodHysteresis selector({100.0, 110.0}, 20.0);
    (void)selector.update(50.0);
    const auto entering = selector.update(97.5); // Inside the capped [95, 105] band.
    CHECK(entering.fading);
    CHECK(entering.fromLevel == 0);
    CHECK(entering.toLevel == 1);
}
