// Public toon facade tests. Includes only public headers.
#include <doctest/doctest.h>

#include "starforge/application.hpp"
#include "starforge/math.hpp"
#include "starforge/renderer.hpp"
#include "starforge/toon.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
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

[[nodiscard]] starforge::Toon::Context demoContext()
{
    constexpr float kPi = 3.141592653589793f;
    const auto view = starforge::math::lookAt(
        starforge::math::Vec3f{0.0f, 0.0f, 5.0f}, starforge::math::Vec3f{0.0f, 0.0f, 0.0f},
        starforge::math::kUpY);
    starforge::Toon::Context context;
    context.viewProj = starforge::math::perspectiveVulkan(kPi / 3.0f, 4.0f / 3.0f, 0.1f, 100.0f) * view;
    context.modelView = view;
    context.lightDirectionView = {0.0f, 0.0f, 1.0f};
    return context;
}

} // namespace

TEST_CASE("Public Toon: is move-only")
{
    static_assert(!std::is_copy_constructible_v<starforge::Toon>);
    static_assert(!std::is_copy_assignable_v<starforge::Toon>);
    static_assert(std::is_move_constructible_v<starforge::Toon>);
    static_assert(std::is_move_assignable_v<starforge::Toon>);
    CHECK(true);
}

TEST_CASE("Public Toon: missing shader artifacts are reported as API errors")
{
    if (!hasDisplay())
    {
        WARN("Skipping toon creation: no Wayland/X11 display is available");
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

    auto toon = starforge::Toon::create(*renderer, {.shaderDirectory = "/nonexistent/starforge-shaders"});
    REQUIRE(!toon.has_value());
    CHECK(toon.error().find("Cannot read shader file") != std::string::npos);
}

TEST_CASE("Public Toon: renders a cel-shaded frame through the public renderer")
{
    if (!hasDisplay())
    {
        WARN("Skipping toon rendering: no Wayland/X11 display is available");
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

    auto toon = starforge::Toon::create(*renderer, {});
    REQUIRE_MESSAGE(toon.has_value(), toon.error());

    const auto context = demoContext();
    starforge::Toon::Params params;
    params.baseColor = {0.5f, 0.25f, 0.2f, 1.0f};
    params.shadowColor = {0.25f, 0.05f, 0.1f, 1.0f};

    auto frame = toon->render(*renderer, context, params);
    REQUIRE_MESSAGE(frame.has_value(), frame.error());
    CHECK(*frame == starforge::Renderer::FrameResult::Presented);

    renderer->waitIdle();
    auto recreated = toon->recreate(*renderer);
    REQUIRE_MESSAGE(recreated.has_value(), recreated.error());

    auto moved = std::move(*toon);
    auto movedFrame = toon->render(*renderer, context, params);
    CHECK(!movedFrame.has_value());
    CHECK(movedFrame.error() == "Toon is moved-from");
    auto movedRender = moved.render(*renderer, context, params);
    REQUIRE_MESSAGE(movedRender.has_value(), movedRender.error());
    CHECK(*movedRender == starforge::Renderer::FrameResult::Presented);

    renderer->waitIdle();
}
