#include <doctest/doctest.h>
#include "ui_menu.hpp"
using namespace starforge::game;

TEST_CASE("M16 polish: pause menu buttons map to actions")
{
    const auto buttons = menuButtons();
    REQUIRE(buttons.size() >= 3);
    CHECK(menuActionFor("resume") == MenuAction::Resume);
    CHECK(menuActionFor("map") == MenuAction::ToggleMap);
    CHECK(menuActionFor("quit") == MenuAction::Quit);
    CHECK(menuActionFor("missing") == MenuAction::None);
}
