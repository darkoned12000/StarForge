// M16.5 (T1): target cycling, selection persistence and lock state.
#include <doctest/doctest.h>

#include "targeting.hpp"

using namespace starforge::game;
using namespace starforge;

namespace
{
std::vector<TargetContact> contacts()
{
    return {{.id = "rock-1", .hostile = false, .distance = 300.0f},
            {.id = "hostile-1", .hostile = true, .distance = 250.0f,
             .shield = 60.0f, .hull = 80.0f,
             .direction = {0.0f, 0.0f, 1.0f}},
            {.id = "station-a", .hostile = false, .distance = 1200.0f}};
}
} // namespace

TEST_CASE("M16.5: cycling preserves selection by id across frame rebuilds")
{
    TargetingState state;
    state.setContacts(contacts());
    CHECK_FALSE(state.hasSelection()); // no auto-target at startup
    CHECK(state.count() == 3);

    state.cycleNext();
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "rock-1");

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
    state.cycleNext(); // rock-1
    state.cycleNext(); // hostile-1 at 250 m
    CHECK_FALSE(state.locked());
    CHECK(state.lockProgress() == 0.0f);

    state.tick(0.125f, true);
    CHECK(state.lockProgress() == doctest::Approx(0.5f));
    state.tick(0.125f, true);
    CHECK(state.locked());

    // Releasing resets.
    state.tick(0.1f, false);
    CHECK(state.lockProgress() == 0.0f);

    // Friendly target cannot lock.
    state.setContacts(contacts()); // selection still hostile-1
    state.cyclePrevious();          // rock-1 (friendly)
    CHECK(state.selected()->id == "rock-1");
    state.tick(2.0f, true);
    CHECK_FALSE(state.locked());

    // Out-of-range hostile cannot lock.
    state.setContacts({{.id = "far", .hostile = true,
                          .distance = TargetingState::kLockRange + 1.0f,
                          .shield = 10.0f, .hull = 10.0f}});
    state.tick(2.0f, true);
    CHECK_FALSE(state.locked());
}

TEST_CASE("M16.5: clearing handles empty contact lists")
{
    TargetingState state;
    CHECK(state.selected() == nullptr);
    CHECK(state.index() == TargetingState::kNoSelection);
    state.cycleNext();
    state.cyclePrevious();
    CHECK_FALSE(state.hasSelection());
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

    state.setContacts({{.id = "far-hostile", .hostile = true, .distance = 500.0f,
                        .shield = 10.0f, .hull = 10.0f},
                       {.id = "near-rock", .hostile = false, .distance = 50.0f},
                       {.id = "close-hostile", .hostile = true, .distance = 120.0f,
                        .shield = 10.0f, .hull = 10.0f}});
    state.lockNearest();
    CHECK(state.selected()->id == "close-hostile");
}

TEST_CASE("M16 polish: T toggles lock on / off and survives rebuilds")
{
    TargetingState state;
    state.setContacts(contacts());
    CHECK_FALSE(state.hasSelection());

    state.toggleLock();
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "hostile-1");

    // Frame rebuild keeps the lock by id.
    state.setContacts(contacts());
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "hostile-1");

    // Pressing T again un-targets without dropping the contact list.
    state.toggleLock();
    CHECK_FALSE(state.hasSelection());
    CHECK(state.count() == 3);

    // Locking again works.
    state.toggleLock();
    CHECK(state.selected()->id == "hostile-1");

    // If a selected contact vanishes on rebuild, selection clears.
    state.setContacts({{.id = "station-a", .hostile = false, .distance = 1200.0f}});
    CHECK_FALSE(state.hasSelection());
}

TEST_CASE("M17.5a: reticle lock picks the hostile nearest the crosshair")
{
    TargetContact left{"left", "Flanker", gameplay::ui_relation::Relation::Hostile, true,
                       900.0f, 60.0f, 80.0f, {-0.7f, 0.0f, 0.71f}};
    TargetContact ahead{"ahead", "Interceptor", gameplay::ui_relation::Relation::Hostile, true,
                        300.0f, 60.0f, 80.0f, {0.0f, 0.0f, 1.0f}};
    TargetContact neutral{"station", "Station", gameplay::ui_relation::Relation::Neutral, false,
                          40.0f, 0.0f, 0.0f, {0.0f, 0.0f, 1.0f}};
    TargetingState state;
    state.setContacts({left, neutral, ahead});

    state.lockNearestHostileToReticle();
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "ahead"); // not the nearer neutral
    CHECK(state.selected()->name == "Interceptor");

    state.toggleReticleLock();
    CHECK_FALSE(state.hasSelection());

    // A friendly cannot be picked even when it is closer to the reticle.
    state.setContacts({neutral, left});
    state.lockNearestHostileToReticle();
    REQUIRE(state.selected() != nullptr);
    CHECK(state.selected()->id == "left");
}
