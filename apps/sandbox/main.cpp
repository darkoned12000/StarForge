// STARFORGE sandbox: public API example. Uses only include/starforge/
// headers and the starforge_public_api target — no Vulkan, SDL, or GLM
// types are visible here.

#include "starforge/application.hpp"
#include "starforge/math.hpp"
#include "starforge/renderer.hpp"
#include "starforge/starfield.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace
{

constexpr float kPi = 3.141592653589793f;

void logInfo(const char* message)
{
    std::printf("[STARFORGE] %s\n", message);
    std::fflush(stdout);
}

void logError(const char* message)
{
    std::fprintf(stderr, "[STARFORGE] %s\n", message);
}

} // namespace

int main()
{
    logInfo("STARFORGE Graphics Technology (public API sandbox)");

    auto app = starforge::Application::create({
        .title = "STARFORGE - Space Graphics Technology",
        .width = 1280,
        .height = 720,
        .resizable = true,
    });
    if (!app)
    {
        logError(app.error().c_str());
        return EXIT_FAILURE;
    }

    auto renderer = starforge::Renderer::create(*app, {});
    if (!renderer)
    {
        logError(renderer.error().c_str());
        return EXIT_FAILURE;
    }

    auto starfield = starforge::Starfield::create(*renderer, {});
    if (!starfield)
    {
        logError(starfield.error().c_str());
        return EXIT_FAILURE;
    }

    logInfo("Close the window to exit");

    bool needRecreate = false;
    float time = 0.0f;
    auto lastFrame = std::chrono::steady_clock::now();

    while (!app->shouldClose())
    {
        app->pollEvents();

        if (app->takeResized())
        {
            needRecreate = true;
        }

        const auto size = app->framebufferSize();
        if (app->isMinimized() || size.width == 0 || size.height == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (needRecreate)
        {
            auto recreated = renderer->recreate();
            if (!recreated)
            {
                logError(recreated.error().c_str());
                return EXIT_FAILURE;
            }
            if (*recreated == starforge::Renderer::RecreateResult::FormatChanged)
            {
                auto starfieldRecreated = starfield->recreate(*renderer);
                if (!starfieldRecreated)
                {
                    logError(starfieldRecreated.error().c_str());
                    return EXIT_FAILURE;
                }
            }
            needRecreate = false;
        }

        const auto now = std::chrono::steady_clock::now();
        time += std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;

        const auto aspect = static_cast<float>(size.width) / std::max(static_cast<float>(size.height), 1.0f);

        starforge::Starfield::RenderContext context;
        context.viewProj = starforge::math::perspectiveVulkan(kPi / 3.0f, aspect, 0.1f, 1000.0f) *
                           starforge::math::lookAt(
                               starforge::math::Vec3f{0.0f, 0.0f, 5.0f}, starforge::math::Vec3f{0.0f, 0.0f, 0.0f},
                               starforge::math::kUpY);
        context.invViewProj = starforge::math::inverse(context.viewProj);
        context.time = time;

        starforge::Starfield::Params params;
        params.worldPosition = {0.0f, 0.0f, -50.0f};
        params.radius = 8.0f;
        params.intensity = 1.0f;

        // Composable recording: the starfield records into the frame owned
        // by the renderer; additional facades can record into the same
        // frame before the single presentation.
        auto frame = renderer->renderFrame(context.clearColor, [&](starforge::FrameContext& frameContext) {
            starfield->record(frameContext, context, params);
        });
        if (!frame)
        {
            logError(frame.error().c_str());
            return EXIT_FAILURE;
        }
        if (*frame == starforge::Renderer::FrameResult::SwapchainOutOfDate)
        {
            needRecreate = true;
        }

        // No sleep here: the swapchain present paces the loop. The
        // minimized branch above already sleeps.
    }

    renderer->waitIdle();
    logInfo("Shutting down");
    return EXIT_SUCCESS;
}
