// Public starfield facade tests. Includes only public headers.
#include <doctest/doctest.h>

#include "starforge/application.hpp"
#include "starforge/math.hpp"
#include "starforge/renderer.hpp"
#include "starforge/starfield.hpp"

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

} // namespace

TEST_CASE("Public Starfield: is move-only")
{
    static_assert(!std::is_copy_constructible_v<starforge::Starfield>);
    static_assert(!std::is_copy_assignable_v<starforge::Starfield>);
    static_assert(std::is_move_constructible_v<starforge::Starfield>);
    static_assert(std::is_move_assignable_v<starforge::Starfield>);
    CHECK(true);
}

TEST_CASE("Public Starfield: missing shader artifacts are reported as API errors")
{
    if (!hasDisplay())
    {
        WARN("Skipping starfield creation: no Wayland/X11 display is available");
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

    auto starfield = starforge::Starfield::create(*renderer, {.shaderDirectory = "/nonexistent/starforge-shaders"});
    REQUIRE(!starfield.has_value());
    CHECK(starfield.error().find("Cannot read shader file") != std::string::npos);
}

TEST_CASE("Public Starfield: renders a frame through the public renderer")
{
    if (!hasDisplay())
    {
        WARN("Skipping starfield rendering: no Wayland/X11 display is available");
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

    auto starfield = starforge::Starfield::create(*renderer, {});
    REQUIRE_MESSAGE(starfield.has_value(), starfield.error());

    constexpr float kPi = 3.141592653589793f;
    starforge::Starfield::RenderContext context;
    context.viewProj = starforge::math::perspectiveVulkan(kPi / 3.0f, 320.0f / 240.0f, 0.1f, 1000.0f) *
                       starforge::math::lookAt(
                           starforge::math::Vec3f{0.0f, 0.0f, 5.0f}, starforge::math::Vec3f{0.0f, 0.0f, 0.0f},
                           starforge::math::kUpY);
    context.invViewProj = starforge::math::Mat4f(1.0f);
    context.time = 0.016f;

    starforge::Starfield::Params params;
    params.worldPosition = {0.0f, 0.0f, -50.0f};
    params.radius = 8.0f;
    params.intensity = 1.0f;

    // Composable recording: record into a frame owned by the renderer.
    auto composed = renderer->renderFrame(context.clearColor, [&](starforge::FrameContext& frame) {
        starfield->record(frame, context, params);
    });
    REQUIRE_MESSAGE(composed.has_value(), composed.error());
    CHECK(*composed == starforge::Renderer::FrameResult::Presented);

    auto frame = starfield->render(*renderer, context, params);
    REQUIRE_MESSAGE(frame.has_value(), frame.error());
    CHECK(*frame == starforge::Renderer::FrameResult::Presented);

    renderer->waitIdle();
    auto resized = renderer->recreate();
    REQUIRE_MESSAGE(resized.has_value(), resized.error());
    CHECK(*resized == starforge::Renderer::RecreateResult::Resized);
    auto recreated = starfield->recreate(*renderer);
    REQUIRE_MESSAGE(recreated.has_value(), recreated.error());

    auto moved = std::move(*starfield);
    auto movedFrame = starfield->render(*renderer, context, params);
    CHECK(!movedFrame.has_value());
    CHECK(movedFrame.error() == "Starfield is moved-from");
    auto movedRender = moved.render(*renderer, context, params);
    REQUIRE_MESSAGE(movedRender.has_value(), movedRender.error());
    CHECK(*movedRender == starforge::Renderer::FrameResult::Presented);

    // The backend pipeline must not be destroyed while presented work may
    // still be in flight; wait for the GPU before scope-exit teardown.
    renderer->waitIdle();
}

TEST_CASE("Public Starfield: moved-from renderer is rejected instead of dereferenced")
{
    if (!hasDisplay())
    {
        WARN("Skipping moved-from renderer check: no Wayland/X11 display is available");
        return;
    }

    auto app = starforge::Application::create({
        .title = "STARFORGE Public API",
        .width = 320,
        .height = 240,
        .resizable = false,
    });
    REQUIRE_MESSAGE(app.has_value(), app.error());

    auto live = starforge::Renderer::create(*app, {});
    REQUIRE_MESSAGE(live.has_value(), live.error());
    auto taken = std::move(*live);

    auto rejected = starforge::Starfield::create(*live, {});
    REQUIRE(!rejected.has_value());
    CHECK(rejected.error() == "Starfield::create failed: Renderer is invalid or moved-from");
}
