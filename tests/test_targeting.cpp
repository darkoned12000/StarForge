// M16.5 (T1): target cycling, selection persistence and lock state.
#include <doctest/doctest.h>

#include "targeting.hpp"

using namespace starforge::game;

namespace
{
std::vector<TargetContact> contacts()
{
    return {{"rock-1", false, 300.0f, 0.0f, 0.0f},
            {"hostile-1", true, 250.0f, 60.0f, 80.0f},
            {"station-a", false, 1200.0f, 0.0f, 0.0f}};
}
} // namespace

TEST_CASE("M16.5: cycling preserves selection by id across frame rebuilds")
{
    TargetingState state;
    state.setContacts(contacts());
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "rock-1");
    CHECK(state.count() == 3);

    state.cycleNext();
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "hostile-1");

    // Rebuild list in the same order: selection survives.
    state.setContacts(contacts());
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "hostile-1");

    state.cyclePrevious();
    CHECK(state.selected()->id == "rock-1");
    state.cyclePrevious();
    CHECK(state.selected()->id == "station-a");
    state.cycleNext();
    CHECK(state.selected()->id == "rock-1"); // wrap
}

TEST_CASE("M16.5: lock requires a held trigger on an in-range hostile")
{
    TargetingState state;
    state.setContacts(contacts());
    state.cycleNext(); // hostile-1 at 250 m
    CHECK_FALSE(state.locked());
    CHECK(state.lockProgress() == 0.0f);

    state.tick(0.75f, true);
    CHECK(state.lockProgress() == doctest::Approx(0.5f));
    state.tick(0.75f, true);
    CHECK(state.locked());

    // Releasing resets.
    state.tick(0.1f, false);
    CHECK(state.lockProgress() == 0.0f);

    // Friendly target cannot lock (selection survives by id, so cycle to
    // the friendly contact explicitly).
    state.setContacts(contacts());
    state.cyclePrevious(); // rock-1 (friendly)
    CHECK(state.selected()->id == "rock-1");
    state.tick(2.0f, true);
    CHECK_FALSE(state.locked());

    // Out-of-range hostile cannot lock.
    state.setContacts({{"far", true, TargetingState::kLockRange + 1.0f, 10.0f, 10.0f}});
    state.tick(2.0f, true);
    CHECK_FALSE(state.locked());
}

TEST_CASE("M16.5: clearing handles empty contact lists")
{
    TargetingState state;
    CHECK(state.selected() == nullptr);
    state.cycleNext();
    state.cyclePrevious();
    CHECK(state.index() == 0);
    state.setContacts({});
    CHECK(state.count() == 0);
    CHECK(state.selected() == nullptr);
    state.tick(1.0f, true);
    CHECK_FALSE(state.locked());
    state.clearSelection();
    CHECK(state.count() == 0);
}

TEST_CASE("M16 polish: T lock prefers the nearest hostile")
{
    TargetingState state;
    state.setContacts(contacts());
    state.lockNearest();
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "hostile-1"); // hostile beats nearer rock

    state.setContacts({{"far-hostile", true, 500.0f, 10.0f, 10.0f},
                       {"near-rock", false, 50.0f, 0.0f, 0.0f},
                       {"close-hostile", true, 120.0f, 10.0f, 10.0f}});
    state.lockNearest();
    CHECK(state.selected()->id == "close-hostile");
}
