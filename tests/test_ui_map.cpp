// M16.9 (T1): map projection and jump-range reachability.
#include <doctest/doctest.h>

#include "ui_map.hpp"

using namespace starforge::game;

TEST_CASE("M16.9: map projects systems relative to current location")
{
    const auto& systems = knownSystems();
    REQUIRE(systems.size() >= 3);
    CHECK(systems.front().id == "sol");

    const auto& origin = systems.front();
    const auto self = projectSystem(origin, origin, 100.0f, 1.0f);
    CHECK(self.x == doctest::Approx(0.0f));
    CHECK(self.y == doctest::Approx(0.0f));
    CHECK(self.distance == doctest::Approx(0.0f));
    CHECK(self.reachable);

    const auto& alpha = systems[1];
    const auto p = projectSystem(alpha, origin, 100.0f, 1.0f);
    CHECK(p.x == doctest::Approx((alpha.x - origin.x) * 100.0f));
    CHECK(p.y == doctest::Approx((alpha.y - origin.y) * 100.0f));
    // alpha-centauri is inside the base 1.0 ly range so the haul quest can
    // be completed and the destination reached from the start.
    CHECK(p.distance > 0.0f);
    CHECK(p.distance < 1.0f);
    CHECK(p.reachable);

    // A farther system (proxima) is still gated by jump range.
    const MapSystem far{"far", 5.0f, 5.0f};
    const auto beyond = projectSystem(far, origin, 100.0f, 1.0f);
    CHECK_FALSE(beyond.reachable);
    const auto reachable = projectSystem(far, origin, 100.0f, beyond.distance + 0.1f);
    CHECK(reachable.reachable);
}

TEST_CASE("M16.9: jump range gates reachability")
{
    const MapSystem origin{"a", 0.0f, 0.0f};
    const MapSystem target{"b", 3.0f, 4.0f}; // distance 5
    CHECK_FALSE(projectSystem(target, origin, 50.0f, 4.9f).reachable);
    CHECK(projectSystem(target, origin, 50.0f, 5.0f).reachable);
    CHECK(projectSystem(target, origin, 50.0f, 10.0f).reachable);
}

TEST_CASE("M16 polish: sector drill-in lists notable objects")
{
    const auto sol = starforge::game::sectorObjects("sol");
    REQUIRE(sol.size() >= 3);
    bool hasStation = false, hasHostile = false;
    for (const auto& o : sol)
    {
        if (o.kind == "station") hasStation = true;
        if (o.kind == "hostile") hasHostile = true;
    }
    CHECK(hasStation);
    CHECK(hasHostile);
    CHECK_FALSE(starforge::game::sectorObjects("unknown").empty());
}

TEST_CASE("M16 polish: map layout separates chart from info and labels jumps")
{
    const auto layout = starforge::game::mapLayout(1920.0f, 1080.0f, "sol", 2.0f);
    CHECK(layout.infoSection.x > layout.mapSection.x + layout.mapSection.w);
    CHECK(layout.dividerX > layout.mapSection.x + layout.mapSection.w);
    CHECK(layout.dividerX < layout.infoSection.x);
    CHECK_FALSE(layout.systems.empty());

    CHECK(starforge::game::jumpButtonLabel("barnard", "sol") == "Jump to barnard");
    CHECK(starforge::game::jumpButtonLabel("sol", "sol") == "Select system");
}

TEST_CASE("M16 polish: alpha-centauri is reachable and systems look distinct")
{
    const float dist = starforge::game::systemDistanceLy("sol", "alpha-centauri");
    CHECK(dist > 0.0f);
    CHECK(dist < 1.0f); // inside the base 1.0 ly jump range

    const auto sol = starforge::game::systemVisual("sol");
    const auto ac = starforge::game::systemVisual("alpha-centauri");
    const auto barnard = starforge::game::systemVisual("barnard");
    CHECK(sol.description != ac.description);
    CHECK(ac.planetBase != barnard.planetBase);
    CHECK_FALSE(ac.description.empty());
}
