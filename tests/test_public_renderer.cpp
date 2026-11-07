#include <doctest/doctest.h>

#include "starforge/application.hpp"
#include "starforge/renderer.hpp"

#include <cstdlib>
#include <type_traits>
#include <utility>

namespace
{

[[nodiscard]] bool hasDisplay()
{
#if defined(__linux__)
    return std::getenv("WAYLAND_DISPLAY") != nullptr || std::getenv("DISPLAY") != nullptr;
#else
    return true;
#endif
}

} // namespace


TEST_CASE("Public Renderer: is move-only")
{
    static_assert(!std::is_copy_constructible_v<starforge::Renderer>);
    static_assert(!std::is_copy_assignable_v<starforge::Renderer>);
    static_assert(std::is_move_constructible_v<starforge::Renderer>);
    static_assert(std::is_move_assignable_v<starforge::Renderer>);
    CHECK(true);
}

TEST_CASE("Public Renderer: zero frames in flight is rejected")
{
    if (!hasDisplay())
    {
        WARN("Skipping renderer creation: no Wayland/X11 display is available");
        return;
    }

    auto app = starforge::Application::create({
        .title = "STARFORGE Public API",
        .width = 320,
        .height = 240,
        .resizable = false,
    });
    REQUIRE_MESSAGE(app.has_value(), app.error());

    auto renderer = starforge::Renderer::create(*app, {.framesInFlight = 0});
    REQUIRE(!renderer.has_value());
    CHECK(renderer.error() == "Renderer framesInFlight must be at least 1");
}

TEST_CASE("Public Renderer: creates a swapchain and presents a cleared frame")
{
    if (!hasDisplay())
    {
        WARN("Skipping renderer creation: no Wayland/X11 display is available");
        return;
    }

    auto app = starforge::Application::create({
        .title = "STARFORGE Public API",
        .width = 320,
        .height = 240,
        .resizable = false,
    });
    REQUIRE_MESSAGE(app.has_value(), app.error());

    auto renderer = starforge::Renderer::create(*app, {});
    REQUIRE_MESSAGE(renderer.has_value(), renderer.error());
    CHECK(renderer->extent().width == 320);
    CHECK(renderer->extent().height == 240);

    auto frame = renderer->renderClearedFrame({.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f});
    REQUIRE_MESSAGE(frame.has_value(), frame.error());
    CHECK(*frame == starforge::Renderer::FrameResult::Presented);

    renderer->waitIdle();

    auto moved = std::move(*renderer);
    CHECK(moved.extent().width == 320);
    CHECK(renderer->extent() == starforge::Size2D{});
    auto movedFrame = renderer->renderClearedFrame({});
    CHECK(!movedFrame.has_value());
}
