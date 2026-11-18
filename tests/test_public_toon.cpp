// Public toon facade tests. Includes only public headers.
#include <doctest/doctest.h>

#include "starforge/application.hpp"
#include "starforge/nebula.hpp"
#include "starforge/starfield.hpp"
#include "starforge/math.hpp"
#include "starforge/sun.hpp"
#include "starforge/renderer.hpp"
#include "starforge/toon.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

TEST_CASE("Public Toon: production showcase composes through public API (9.5.6)")
{
    if (!hasDisplay())
    {
        WARN("Skipping showcase composition: no Wayland/X11 display is available");
        return;
    }

    auto app = starforge::Application::create({
        .title = "STARFORGE Toon Showcase", .width = 320, .height = 240, .resizable = false});
    REQUIRE(app.has_value());
    auto renderer = starforge::Renderer::create(*app, {.framesInFlight = 2});
    REQUIRE(renderer.has_value());

    auto toon = starforge::Toon::create(*renderer, {});
    REQUIRE(toon.has_value());
    auto stars = starforge::Starfield::create(*renderer, {});
    REQUIRE(stars.has_value());
    auto nebula = starforge::Nebula::create(*renderer, {});
    REQUIRE(nebula.has_value());
    auto sun = starforge::Sun::create(*renderer, {});
    REQUIRE(sun.has_value());

    // A small procedural hull (tetrahedral wedge) uploaded as a toon mesh.
    std::vector<starforge::Toon::MeshVertex> vertices;
    auto vertex = [](float x, float y, float z) {
        return starforge::Toon::MeshVertex{{x, y, z}, {0.0f, 1.0f, 0.0f}, {x, y, z}};
    };
    vertices.push_back(vertex(0.0f, 0.5f, 0.0f));
    vertices.push_back(vertex(-0.5f, -0.5f, 0.5f));
    vertices.push_back(vertex(0.5f, -0.5f, 0.5f));
    auto mesh = toon->createMesh(vertices);
    REQUIRE(mesh.has_value());
    CHECK(mesh->vertexCount() == 3);

    auto context = demoContext();
    starforge::Toon::Params material;
    material.capabilities = 1u | 8u; // emissive + scattering rim, one shader
    material.emissiveColor = {0.5f, 1.0f, 1.5f, 0.8f};
    material.fresnelColor = {0.3f, 0.6f, 1.0f, 0.7f};

    starforge::Starfield::RenderContext starContext;
    starContext.viewProj = context.viewProj;
    starContext.invViewProj = starforge::math::inverse(context.viewProj);
    starContext.resolution = {320.0f, 240.0f};
    starforge::Nebula::RenderContext nebulaContext;
    nebulaContext.viewProj = context.viewProj;
    nebulaContext.invViewProj = starContext.invViewProj;
    nebulaContext.resolution = starContext.resolution;

    auto frame = renderer->renderFrame({}, [&](starforge::FrameContext& fc) {
        nebula->record(fc, nebulaContext, {});
        stars->record(fc, starContext, {});
        toon->recordMesh(fc, context, material, *mesh);
    });
    REQUIRE(frame.has_value());
    CHECK(*frame == starforge::Renderer::FrameResult::Presented);

    renderer->waitIdle();
}
