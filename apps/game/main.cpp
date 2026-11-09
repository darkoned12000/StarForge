// First Flight (M15.0.1 + M15.0.2): playable demo.
// Game shell: fixed-step simulation decoupled from rendering.
// Player ship: Freelancer controls on an ECS ship, chase camera,
// toon ship + starfield via the public API.
//
// Human-readable by design: one clear loop, small helpers, all world
// positions via starforge::WorldPosition / camera-relative math.
// This TU uses the public API only; ECS access goes through the raw
// game bridge (no TU includes both public and internal math).

#include "starforge/application.hpp"
#include "starforge/math.hpp"
#include "starforge/renderer.hpp"
#include "starforge/starfield.hpp"
#include "starforge/toon.hpp"
#include "starforge/world.hpp"

#include "game_bridge.hpp"
#include "ship_controller.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace
{

constexpr float kPi = 3.141592653589793f;
constexpr float kSimStep = 1.0f / 60.0f; // fixed 60 Hz simulation

void logInfo(const char* message)
{
    std::printf("[STARFORGE] %s\n", message);
    std::fflush(stdout);
}

void logError(const char* message)
{
    std::fprintf(stderr, "[STARFORGE] %s\n", message);
}

// Rotation matrix (column-major) from a unit quaternion (xyzw).
starforge::math::Mat4f matFromQuat(starforge::math::Vec4f q)
{
    const float x = q.x(), y = q.y(), z = q.z(), w = q.w();
    starforge::math::Mat4f m(1.0f);
    m.columns[0] = {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + w * z), 2.0f * (x * z - w * y), 0.0f};
    m.columns[1] = {2.0f * (x * y - w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + w * x), 0.0f};
    m.columns[2] = {2.0f * (x * z + w * y), 2.0f * (y * z - w * x), 1.0f - 2.0f * (x * x + y * y), 0.0f};
    m.columns[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

// World-space forward direction (-Z in ship space) from the same quaternion.
starforge::math::Vec3d forwardFromQuat(starforge::math::Vec4f q)
{
    const starforge::math::Mat4f m = matFromQuat(q);
    return starforge::math::Vec3d{-m.columns[2][0], -m.columns[2][1], -m.columns[2][2]};
}

// World-space up direction (+Y in ship space) from the same quaternion.
starforge::math::Vec3d upFromQuat(starforge::math::Vec4f q)
{
    const starforge::math::Mat4f m = matFromQuat(q);
    return starforge::math::Vec3d{m.columns[1][0], m.columns[1][1], m.columns[1][2]};
}

[[nodiscard]] starforge::math::Vec3f toVec3f(starforge::math::Vec3d v)
{
    return starforge::math::Vec3f{static_cast<float>(v.x()), static_cast<float>(v.y()),
                                   static_cast<float>(v.z())};
}

// Build the input edges for this frame from the public API.
struct InputTracker
{
    bool prevW = false;
    bool prevS = false;
    bool prevSpace = false;
    bool prevPause = false;
};

[[nodiscard]] starforge::game::InputEdge readInput(starforge::Application& app, InputTracker& tracker)
{
    starforge::game::InputEdge edge;
    const bool w = app.isKeyDown(starforge::Key::W);
    const bool s = app.isKeyDown(starforge::Key::S);
    const bool space = app.isKeyDown(starforge::Key::Space);
    const bool pause = app.isKeyDown(starforge::Key::P) || app.isKeyDown(starforge::Key::Escape);

    edge.wPressed = w && !tracker.prevW;
    edge.sPressed = s && !tracker.prevS;
    edge.spacePressed = space && !tracker.prevSpace;
    edge.pausePressed = pause && !tracker.prevPause;
    edge.aDown = app.isKeyDown(starforge::Key::A);
    edge.dDown = app.isKeyDown(starforge::Key::D);

    tracker.prevW = w;
    tracker.prevS = s;
    tracker.prevSpace = space;
    tracker.prevPause = pause;

    const auto motion = app.takeMouseMotion();
    edge.mouseDx = motion.dx;
    edge.mouseDy = motion.dy;
    edge.mouseLeftDown = app.isMouseButtonDown(starforge::MouseButton::Left);
    edge.mouseRightDown = app.isMouseButtonDown(starforge::MouseButton::Right);
    return edge;
}

} // namespace

int main()
{
    logInfo("First Flight: shell starting");

    auto app = starforge::Application::create({
        .title = "STARFORGE - First Flight",
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

    auto toon = starforge::Toon::create(*renderer, {});
    if (!toon)
    {
        logError(toon.error().c_str());
        return EXIT_FAILURE;
    }

    // Game layer: ECS world holds the player ship (raw bridge).
    starforge::game::GameWorld world;
    starforge::game::FlightState flight;
    logInfo("First Flight: ship created. W/S throttle, mouse steer, Space UI, P pause.");

    bool needRecreate = false;
    float time = 0.0f;
    float accumulator = 0.0f;
    float hudTimer = 0.0f;
    auto lastFrame = std::chrono::steady_clock::now();

    // Input carried across frames so taps and mouse deltas are never
    // lost on frames with zero fixed steps (display > 60 Hz).
    starforge::game::InputEdge pending;
    InputTracker inputTracker;
    float lastThrottle = -1.0f;
    bool lastPaused = false;
    starforge::game::FlightMode lastMode = starforge::game::FlightMode::Flight;
    bool lastFirePrimary = false;
    bool lastFireSecondary = false;
    bool lastRelativeMouse = false;

    while (!app->shouldClose())
    {
        app->pollEvents();
        if (app->shouldClose())
        {
            break;
        }
        if (app->takeResized())
        {
            needRecreate = true;
        }

        const auto now0 = std::chrono::steady_clock::now();
        float frameDt = std::chrono::duration<float>(now0 - lastFrame).count();
        lastFrame = now0;
        frameDt = std::min(std::max(frameDt, 0.0f), 0.25f); // spiral-of-death guard

        const auto size = app->framebufferSize();
        if (app->isMinimized() || size.width == 0 || size.height == 0)
        {
            accumulator = 0.0f; // flush time so resume doesn't play catch-up
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
                if (auto sf = starfield->recreate(*renderer); !sf)
                {
                    logError(sf.error().c_str());
                    return EXIT_FAILURE;
                }
                if (auto t = toon->recreate(*renderer); !t)
                {
                    logError(t.error().c_str());
                    return EXIT_FAILURE;
                }
            }
            needRecreate = false;
        }

        if (!flight.paused)
        {
            time += frameDt;
        }

        // Merge this frame's input into `pending` first...
        {
            const auto edge = readInput(*app, inputTracker);
            pending.wPressed = pending.wPressed || edge.wPressed;
            pending.sPressed = pending.sPressed || edge.sPressed;
            pending.spacePressed = pending.spacePressed || edge.spacePressed;
            pending.pausePressed = pending.pausePressed || edge.pausePressed;
            pending.aDown = edge.aDown;
            pending.dDown = edge.dDown;
            pending.mouseDx += edge.mouseDx;
            pending.mouseDy += edge.mouseDy;
            pending.mouseLeftDown = edge.mouseLeftDown;
            pending.mouseRightDown = edge.mouseRightDown;
        }

        // ...then consume BUTTON EDGES immediately (never gated on the
        // fixed-step accumulator — MAILBOX uncapped fps starved them
        // while paused, the reported "cannot un-pause" bug).
        // Continuum (mouse steer, weapons) runs inside sim steps.
        {
            if (pending.pausePressed)
            {
                flight.paused = !flight.paused;
                pending.pausePressed = false;
            }
            if (!flight.paused)
            {
                if (pending.spacePressed)
                {
                    flight.mode = flight.mode == starforge::game::FlightMode::Flight
                                      ? starforge::game::FlightMode::Ui
                                      : starforge::game::FlightMode::Flight;
                    pending.spacePressed = false;
                }
                if (pending.wPressed)
                {
                    flight.throttle = std::min(1.0f, flight.throttle + starforge::game::kThrottleStep);
                    pending.wPressed = false;
                }
                if (pending.sPressed)
                {
                    flight.throttle = std::max(0.0f, flight.throttle - starforge::game::kThrottleStep);
                    pending.sPressed = false;
                }
            }
            if (flight.paused)
            {
                // While paused only the unpause edge is live: discard
                // throttle / mode taps so the ship cannot "speed up"
                // during the pause menu (matches applyInput).
                pending.wPressed = false;
                pending.sPressed = false;
                pending.spacePressed = false;
                // Discard steering deltas while paused so unpausing
                // doesn't deliver a stale jump.
                pending.mouseDx = 0.0f;
                pending.mouseDy = 0.0f;
                pending.mouseLeftDown = false;
                pending.mouseRightDown = false;
                accumulator = 0.0f;
            }
        }

        // --- Fixed-step simulation (M15.0.1) ---
        if (!flight.paused)
        {
            accumulator += frameDt;
        }
        int steps = 0;
        while (accumulator >= kSimStep && steps < 5 && !flight.paused) // cap: avoid spiral of death
        {
            const auto stepEdge = pending; // snapshot; edges already consumed
            starforge::game::applyInput(flight, stepEdge, kSimStep);

            const auto ship = world.shipTransform();
            const auto forward = forwardFromQuat(
                {ship.orientation.x, ship.orientation.y, ship.orientation.z, ship.orientation.w});
            const float speed = starforge::game::targetSpeed(flight);
            world.setVelocity(
                {forward.x() * speed, forward.y() * speed, forward.z() * speed},
                {flight.pitchRate, flight.yawRate, flight.rollRate});
            world.step(kSimStep);
            accumulator -= kSimStep;
            ++steps;
            // Mouse deltas are consumed by the first step; held levels
            // (A/D roll, mouse buttons) must survive every sub-step.
            pending.mouseDx = 0.0f;
            pending.mouseDy = 0.0f;
        }

        // --- Console HUD + edge logs (troubleshooting feedback) ---
        if (flight.throttle != lastThrottle)
        {
            std::printf("[INPUT] throttle %.0f%%\n", flight.throttle * 100.0f);
            std::fflush(stdout);
            lastThrottle = flight.throttle;
        }
        if (flight.paused != lastPaused)
        {
            std::printf("[INPUT] %s\n", flight.paused ? "PAUSED" : "UNPAUSED");
            std::fflush(stdout);
            lastPaused = flight.paused;
        }
        if (flight.mode != lastMode)
        {
            std::printf("[INPUT] mode: %s\n",
                        flight.mode == starforge::game::FlightMode::Ui ? "UI" : "FLIGHT");
            std::fflush(stdout);
            lastMode = flight.mode;
        }
        if (flight.firePrimary && !lastFirePrimary)
        {
            std::printf("[INPUT] PRIMARY FIRE (held)\n");
            std::fflush(stdout);
        }
        if (flight.fireSecondary && !lastFireSecondary)
        {
            std::printf("[INPUT] SECONDARY FIRE\n");
            std::fflush(stdout);
        }
        lastFirePrimary = flight.firePrimary;
        lastFireSecondary = flight.fireSecondary;

        hudTimer += frameDt;
        if (hudTimer >= 1.0f)
        {
            hudTimer = 0.0f;
            const auto ship = world.shipTransform();
            // While paused the ship velocity is zero, not the throttle
            // target — show effective speed so it reads as stopped.
            const float effectiveSpeed = flight.paused ? 0.0f : starforge::game::targetSpeed(flight);
            std::printf(
                "[HUD] throttle %.0f%% speed %.0f m/s pos (%.0f, %.0f, %.0f) mode %s%s\n",
                flight.throttle * 100.0f, effectiveSpeed, ship.position.x, ship.position.y,
                ship.position.z,
                flight.mode == starforge::game::FlightMode::Ui ? "UI" : "FLIGHT",
                flight.paused ? " PAUSED" : "");
            std::fflush(stdout);
        }

        // Mouse-trapping: Flight mode captures the cursor for steering;
        // UI mode / pause frees it for clicking.
        {
            const bool wantRelative =
                flight.mode == starforge::game::FlightMode::Flight && !flight.paused;
            if (wantRelative != lastRelativeMouse)
            {
                app->setRelativeMouseMode(wantRelative);
                lastRelativeMouse = wantRelative;
            }
        }

        // --- Rendering (camera-relative) ---
        // Chase camera follows the ship: eye sits behind and above the
        // ship, looking ahead down the forward vector. No astronomical
        // coordinates reach the GPU — everything is ship-relative.
        const auto ship = world.shipTransform();
        const starforge::math::Vec4f shipQuat{
            ship.orientation.x, ship.orientation.y, ship.orientation.z, ship.orientation.w};
        const starforge::math::Vec3f shipForward = toVec3f(forwardFromQuat(shipQuat));
        const starforge::math::Vec3f shipUp = toVec3f(upFromQuat(shipQuat));
        // Camera-relative offsets (ship at origin):
        const starforge::math::Vec3f eyeOffset = {
            -shipForward.x() * 8.0f + shipUp.x() * 2.5f,
            -shipForward.y() * 8.0f + shipUp.y() * 2.5f,
            -shipForward.z() * 8.0f + shipUp.z() * 2.5f};
        const starforge::math::Vec3f lookAhead = {
            shipForward.x() * 10.0f, shipForward.y() * 10.0f, shipForward.z() * 10.0f};

        const float aspect = static_cast<float>(size.width) / std::max(static_cast<float>(size.height), 1.0f);
        const auto view = starforge::math::lookAt(eyeOffset, lookAhead, shipUp);
        const auto proj = starforge::math::perspectiveVulkan(kPi / 3.0f, aspect, 0.1f, 1000.0f);
        // Ship sits at the ship-relative origin; lookAt already places it
        // 8 m ahead of the camera. Adding another translation would
        // double-shift it (review fix).
        const auto model = matFromQuat(shipQuat);

        starforge::Starfield::RenderContext starContext;
        starContext.viewProj = proj * view;
        starContext.invViewProj = starforge::math::inverse(starContext.viewProj);
        starContext.time = time;
        if (flight.paused)
        {
            // Dark red tint while paused: visible without needing the console.
            starContext.clearColor = {.red = 0.25f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f};
        }

        starforge::Starfield::Params sunParams;
        // Sun is a fixed world object, not camera-anchored. Compute it
        // camera-relative each frame (sunWorld - shipPos in double, cast
        // once) so it stays put in the sky and only drifts as the ship
        // turns or travels — it does not follow the camera.
        {
            constexpr starforge::game::RawVec3d kSunWorld{50000.0, 10000.0, -100000.0};
            const auto ship = world.shipTransform();
            sunParams.worldPosition = {
                static_cast<float>(kSunWorld.x - ship.position.x),
                static_cast<float>(kSunWorld.y - ship.position.y),
                static_cast<float>(kSunWorld.z - ship.position.z)};
            sunParams.radius = 5000.0f; // ~3° at ~110 km
            sunParams.intensity = 1.0f;
        }

        starforge::Toon::Context toonContext;
        toonContext.viewProj = proj * view * model;
        toonContext.modelView = view * model;
        toonContext.lightDirectionView =
            starforge::math::normalize(starforge::math::Vec3f{0.4f, 0.5f, 0.75f});

        starforge::Toon::Params toonParams;
        toonParams.baseColor = {0.85f, 0.25f, 0.2f, 1.0f};
        toonParams.shadowColor = {0.2f, 0.05f, 0.3f, 1.0f};
        toonParams.bandCount = 3.0f;
        toonParams.rimStrength = 0.6f;
        toonParams.outlineWidth = 2.0f;

        auto frame = renderer->renderFrame(starContext.clearColor, [&](starforge::FrameContext& fc) {
            starfield->record(fc, starContext, sunParams);
            toon->record(fc, toonContext, toonParams);
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

        // Weapon flashes/pause feedback (placeholders until M15.0.5 combat).
        if (flight.firePrimary)
        {
            // Primary guns held (left mouse) — no projectile yet.
        }
    }

    renderer->waitIdle();
    logInfo("Shutting down");
    return EXIT_SUCCESS;
}
