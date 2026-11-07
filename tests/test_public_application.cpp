#include <doctest/doctest.h>

#include "starforge/application.hpp"

#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<starforge::Application>);
static_assert(!std::is_copy_assignable_v<starforge::Application>);
static_assert(std::is_move_constructible_v<starforge::Application>);
static_assert(std::is_move_assignable_v<starforge::Application>);

TEST_CASE("Public Application: invalid sizes are rejected without a window")
{
    auto zeroWidth = starforge::Application::create({
        .title = "STARFORGE Public API",
        .width = 0,
        .height = 720,
    });
    REQUIRE(!zeroWidth.has_value());
    CHECK(zeroWidth.error() == "Application width and height must be non-zero and within valid range");

    auto zeroHeight = starforge::Application::create({
        .title = "STARFORGE Public API",
        .width = 1280,
        .height = 0,
    });
    REQUIRE(!zeroHeight.has_value());
    CHECK(zeroHeight.error() == "Application width and height must be non-zero and within valid range");
}

TEST_CASE("Public Application: empty title is rejected without a window")
{
    auto result = starforge::Application::create({
        .title = "",
        .width = 1280,
        .height = 720,
    });
    REQUIRE(!result.has_value());
    CHECK(result.error() == "Application title must not be empty");
}

TEST_CASE("Public Application: moved-from instance is safe to query")
{
    if (std::getenv("WAYLAND_DISPLAY") == nullptr && std::getenv("DISPLAY") == nullptr)
    {
        WARN("Skipping valid window creation: no Wayland/X11 display is available");
        return;
    }

    auto app = starforge::Application::create({
        .title = "STARFORGE Public API",
        .width = 320,
        .height = 240,
        .resizable = false,
    });

    REQUIRE_MESSAGE(app.has_value(), app.error());
    CHECK(app->title() == "STARFORGE Public API");
    CHECK(app->framebufferSize().width == 320);
    CHECK(app->framebufferSize().height == 240);

    auto moved = std::move(*app);
    REQUIRE(moved.title() == "STARFORGE Public API");
    CHECK(app->shouldClose());
    CHECK(app->isMinimized());
    CHECK_FALSE(app->takeResized());
    CHECK(app->framebufferSize().width == 0);
    CHECK(app->framebufferSize().height == 0);
    CHECK(app->title().empty());
}
