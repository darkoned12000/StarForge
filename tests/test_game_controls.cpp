#include <doctest/doctest.h>

#include "ship_controller.hpp"

TEST_CASE("Flight: throttle persists after W/S taps")
{
    starforge::game::FlightState state;
    CHECK(state.throttle == 0.0f);

    starforge::game::InputEdge tapW;
    tapW.wPressed = true;
    starforge::game::applyInput(state, tapW, 1.0f / 60.0f);
    CHECK(state.throttle == 0.25f);
    CHECK(starforge::game::targetSpeed(state) == 15.0f);

    // Holding W (no edge) does not change throttle further.
    starforge::game::InputEdge holdW;
    starforge::game::applyInput(state, holdW, 1.0f / 60.0f);
    CHECK(state.throttle == 0.25f);

    starforge::game::InputEdge tapS;
    tapS.sPressed = true;
    starforge::game::applyInput(state, tapS, 1.0f / 60.0f);
    CHECK(state.throttle == 0.0f);
}

TEST_CASE("Flight: throttle clamps at 0 and 1")
{
    starforge::game::FlightState state;
    for (int i = 0; i < 10; ++i)
    {
        starforge::game::InputEdge tap;
        tap.wPressed = true;
        starforge::game::applyInput(state, tap, 1.0f / 60.0f);
    }
    CHECK(state.throttle == 1.0f);
    CHECK(starforge::game::targetSpeed(state) == starforge::game::kMaxSpeed);

    for (int i = 0; i < 10; ++i)
    {
        starforge::game::InputEdge tap;
        tap.sPressed = true;
        starforge::game::applyInput(state, tap, 1.0f / 60.0f);
    }
    CHECK(state.throttle == 0.0f);
}

TEST_CASE("Flight: mouse steering in flight mode, ignored in UI mode")
{
    starforge::game::FlightState state;
    starforge::game::InputEdge input;
    input.mouseDx = 100.0f; // right
    input.mouseDy = -50.0f; // up (pitch up)
    starforge::game::applyInput(state, input, 1.0f / 60.0f);
    CHECK(state.yawRate < 0.0f);  // right mouse = turn right (negative yaw)
    CHECK(state.pitchRate > 0.0f); // up mouse = nose up

    state.mode = starforge::game::FlightMode::Ui;
    state.pitchRate = 0.0f;
    state.yawRate = 0.0f;
    starforge::game::applyInput(state, input, 1.0f / 60.0f);
    CHECK(state.pitchRate == 0.0f);
    CHECK(state.yawRate == 0.0f);
}

TEST_CASE("Flight: A/D barrel roll held")
{
    starforge::game::FlightState state;
    starforge::game::InputEdge a;
    a.aDown = true;
    starforge::game::applyInput(state, a, 1.0f / 60.0f);
    CHECK(state.rollRate < 0.0f);

    starforge::game::InputEdge d;
    d.dDown = true;
    starforge::game::applyInput(state, d, 1.0f / 60.0f);
    CHECK(state.rollRate > 0.0f);
}

TEST_CASE("Flight: Space toggles mode, P/Esc toggles pause")
{
    starforge::game::FlightState state;
    starforge::game::InputEdge space;
    space.spacePressed = true;
    starforge::game::applyInput(state, space, 1.0f / 60.0f);
    CHECK(state.mode == starforge::game::FlightMode::Ui);

    starforge::game::applyInput(state, space, 1.0f / 60.0f);
    CHECK(state.mode == starforge::game::FlightMode::Flight);

    starforge::game::InputEdge pause;
    pause.pausePressed = true;
    starforge::game::applyInput(state, pause, 1.0f / 60.0f);
    CHECK(state.paused);

    starforge::game::applyInput(state, pause, 1.0f / 60.0f);
    CHECK_FALSE(state.paused);
}

TEST_CASE("Flight: weapon states follow buttons, suppressed while paused")
{
    starforge::game::FlightState state;
    starforge::game::InputEdge input;
    input.mouseLeftDown = true;
    input.mouseRightDown = true;
    starforge::game::applyInput(state, input, 1.0f / 60.0f);
    CHECK(state.firePrimary);
    CHECK(state.fireSecondary);

    starforge::game::InputEdge pause;
    pause.pausePressed = true;
    starforge::game::applyInput(state, pause, 1.0f / 60.0f);
    CHECK_FALSE(state.firePrimary);
    CHECK_FALSE(state.fireSecondary);
}
