// STARFORGE sandbox: public API example. Uses only include/starforge/
// headers and the starforge_public_api target — no Vulkan, SDL, or GLM
// types are visible here.

#include "starforge/application.hpp"
#include "starforge/audio.hpp"
#include "starforge/math.hpp"
#include "starforge/renderer.hpp"
#include "starforge/starfield.hpp"
#include "starforge/toon.hpp"
#include "starforge/world.hpp"

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

    // Cel-shaded ship stand-in (static cube until M17 mesh assets land).
    auto toon = starforge::Toon::create(*renderer, {});
    if (!toon)
    {
        logError(toon.error().c_str());
        return EXIT_FAILURE;
    }

    // M12 flyby: a looped test tone whose emitter crosses in front of
    // the listener, so distance attenuation and the Doppler sweep are
    // audible. Falls back to silence (no audio device) without failing.
    std::optional<starforge::Audio> audio;
    std::optional<starforge::Audio::VoiceHandle> flyby;
    {
        starforge::Audio::CreateInfo audioInfo;
        audioInfo.backend = starforge::Audio::CreateInfo::Backend::Default;
        auto created = starforge::Audio::create(audioInfo);
        if (!created)
        {
            logError("Audio unavailable (continuing silent)");
        }
        else
        {
            audio = std::move(*created);
            starforge::Audio::SoundParams params;
            params.bus = starforge::Audio::Bus::SFX;
            params.loop = true;
            const auto wav = starforge::generateToneWav(48000, 2, 440.0f, 0.5f, 0.5f);
            auto handle = audio->loadFromMemory(wav, params);
            if (!handle)
            {
                logError(handle.error().c_str());
                audio.reset();
            }
            else
            {
                flyby = *handle;
                (void)audio->play(*handle);
                logInfo("M12 flyby: tone loop audible (flyby passing)");
            }
        }
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
                auto toonRecreated = toon->recreate(*renderer);
                if (!toonRecreated)
                {
                    logError(toonRecreated.error().c_str());
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
        // Sun offset from the view axis so it is not occluded by the ship.
        params.worldPosition = {18.0f, 8.0f, -50.0f};
        params.radius = 10.0f;
        params.intensity = 1.0f;

        // Cel-shaded ship stand-in, offset left of center.
        const auto view = starforge::math::lookAt(
            starforge::math::Vec3f{0.0f, 0.0f, 5.0f}, starforge::math::Vec3f{0.0f, 0.0f, 0.0f},
            starforge::math::kUpY);
        starforge::math::Mat4f shipModel(1.0f);
        shipModel.columns[3][0] = -2.5f;
        shipModel.columns[3][1] = 0.3f;

        starforge::Toon::Context toonContext;
        toonContext.viewProj =
            starforge::math::perspectiveVulkan(kPi / 3.0f, aspect, 0.1f, 1000.0f) * view * shipModel;
        toonContext.modelView = view * shipModel;
        toonContext.lightDirectionView = starforge::math::normalize(
            starforge::math::Vec3f{0.4f, 0.5f, 0.75f});

        starforge::Toon::Params toonParams;
        toonParams.baseColor = {0.85f, 0.25f, 0.2f, 1.0f};
        toonParams.shadowColor = {0.2f, 0.05f, 0.3f, 1.0f};
        toonParams.bandCount = 3.0f;
        toonParams.rimStrength = 0.6f;
        toonParams.outlineWidth = 2.0f;

        // Composable recording: starfield background, then the toon
        // ship, both into the frame owned by the renderer before the
        // single presentation.
        auto frame = renderer->renderFrame(context.clearColor, [&](starforge::FrameContext& frameContext) {
            starfield->record(frameContext, context, params);
            toon->record(frameContext, toonContext, toonParams);
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

        // M12 flyby motion: emitter sweeps across the view, crossing
        // closest approach roughly every 10 seconds (16 s period).
        if (audio.has_value() && flyby.has_value())
        {
            constexpr float kPeriodSeconds = 16.0f;
            const float phase = std::fmod(time, kPeriodSeconds) / kPeriodSeconds;
            const float sweep = (phase * 2.0f - 1.0f) * 30.0f; // -30..+30 m/s
            const float closestFactor = std::cos(phase * 2.0f * kPi) * 0.5f + 0.5f;

            if (const auto cameraCow = starforge::WorldPosition::fromMeters(0.0, 0.0, 0.0))
            {
                const auto emitterPos = cameraCow->translated({sweep, 3.0f + closestFactor * 4.0f, -8.0f});
                starforge::Audio::ListenerState listener;
                listener.position = *cameraCow;
                audio->setListener(listener, *cameraCow);

                starforge::Audio::EmitterState emitter;
                emitter.position = emitterPos.value_or(*cameraCow);
                const float dx = 60.0f / kPeriodSeconds; // d(sweep)/dt
                const float dy = -(kPi / 4.0f) * std::sin(2.0f * kPi * phase);
                emitter.velocity = {dx, dy, 0.0};
                audio->setEmitter(*flyby, emitter, *cameraCow, 1.0f);
            }
        }

        // No sleep here: the swapchain present paces the loop. The
        // minimized branch above already sleeps.
    }

    renderer->waitIdle();
    logInfo("Shutting down");
    return EXIT_SUCCESS;
}
