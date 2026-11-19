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
