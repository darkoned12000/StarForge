// M16.4 (T1): UI screens, hit-testing and focus are pure CPU.
#include <doctest/doctest.h>

#include "ui_state.hpp"

using namespace starforge::game;

TEST_CASE("M16.4: hit-testing uses cursor and click edge")
{
    UiState ui;
    ui.beginFrame(15.0f, 20.0f, false);
    ui.addWidget("buy", UiRect{10.0f, 10.0f, 20.0f, 20.0f});
    ui.addWidget("sell", UiRect{40.0f, 10.0f, 20.0f, 20.0f});

    CHECK(ui.hovered("buy"));
    CHECK_FALSE(ui.hovered("sell"));
    CHECK_FALSE(ui.clicked("buy"));
    CHECK(ui.focus() == "buy");

    // Click edge on the next frame over the same widget.
    ui.beginFrame(15.0f, 20.0f, true);
    ui.addWidget("buy", UiRect{10.0f, 10.0f, 20.0f, 20.0f});
    ui.addWidget("sell", UiRect{40.0f, 10.0f, 20.0f, 20.0f});
    CHECK(ui.clicked("buy"));
    CHECK_FALSE(ui.clicked("sell"));

    // Held click (no new edge) does not repeat.
    ui.beginFrame(15.0f, 20.0f, true);
    ui.addWidget("buy", UiRect{10.0f, 10.0f, 20.0f, 20.0f});
    CHECK(ui.clicked("buy"));
    ui.beginFrame(15.0f, 20.0f, false);
    ui.addWidget("buy", UiRect{10.0f, 10.0f, 20.0f, 20.0f});
    CHECK_FALSE(ui.clicked("buy"));
}

TEST_CASE("M16.4: later widgets win on overlap; unknown ids are safe")
{
    UiState ui;
    ui.beginFrame(15.0f, 15.0f, true);
    ui.addWidget("back", UiRect{0.0f, 0.0f, 50.0f, 50.0f});
    ui.addWidget("front", UiRect{10.0f, 10.0f, 20.0f, 20.0f});
    CHECK(ui.hovered("front"));
    CHECK_FALSE(ui.hovered("back"));
    CHECK(ui.clicked("front"));
    CHECK_FALSE(ui.clicked("back"));
    CHECK_FALSE(ui.hovered("missing"));
    CHECK_FALSE(ui.clicked("missing"));
    CHECK(ui.widgetCount() == 2);
}

TEST_CASE("M16.4: focus cycles across per-frame widget rebuilds")
{
    UiState ui;
    const auto rebuild = [&]() {
        ui.beginFrame(-100.0f, -100.0f, false); // cursor off-widget
        ui.addWidget("a", UiRect{0.0f, 0.0f, 10.0f, 10.0f});
        ui.addWidget("b", UiRect{20.0f, 0.0f, 10.0f, 10.0f});
        ui.addWidget("c", UiRect{40.0f, 0.0f, 10.0f, 10.0f});
    };
    rebuild();
    ui.setFocus("a");
    ui.focusNext();
    CHECK(ui.focus() == "b");
    ui.focusNext();
    CHECK(ui.focus() == "c");
    ui.focusNext();
    CHECK(ui.focus() == "a"); // wraps
    rebuild();                  // focus survives rebuild
    CHECK(ui.focus() == "a");
    ui.focusPrevious();
    CHECK(ui.focus() == "c");
}

TEST_CASE("M16.4: screen stack keeps Flight as base")
{
    UiState ui;
    CHECK(ui.screen() == UiScreen::Flight);
    CHECK(ui.stackDepth() == 1);
    ui.pushScreen(UiScreen::Pause);
    CHECK(ui.screen() == UiScreen::Pause);
    CHECK(ui.stackDepth() == 2);
    ui.pushScreen(UiScreen::Menu);
    CHECK(ui.screen() == UiScreen::Menu);
    ui.popScreen();
    CHECK(ui.screen() == UiScreen::Pause);
    ui.popScreen();
    CHECK(ui.screen() == UiScreen::Flight);
    ui.popScreen(); // no underflow
    CHECK(ui.screen() == UiScreen::Flight);
    CHECK(ui.stackDepth() == 1);
    ui.setScreen(UiScreen::Station);
    CHECK(ui.screen() == UiScreen::Station);
    CHECK(ui.stackDepth() == 1);
}
