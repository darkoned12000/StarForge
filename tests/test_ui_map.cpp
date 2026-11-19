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
    CHECK(p.distance > 1.0f);
    CHECK_FALSE(p.reachable); // beyond 1.0 jump range

    const auto reachable = projectSystem(alpha, origin, 100.0f, p.distance + 0.1f);
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
