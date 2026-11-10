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
#include "starforge/nebula.hpp"
#include "starforge/renderer.hpp"
#include "starforge/starfield.hpp"
#include "starforge/toon.hpp"
#include "starforge/world.hpp"

#include "game_bridge.hpp"
#include "ship_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

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

// Uniform scale that keeps w = 1 (Mat4f(s) would set [3][3] = s and
// collapse positions through perspective divide — rocks/planet were
// rendering at metres instead of kilometres).
[[nodiscard]] starforge::math::Mat4f scaleMat(float s)
{
    starforge::math::Mat4f m(1.0f);
    m.columns[0][0] = s;
    m.columns[1][1] = s;
    m.columns[2][2] = s;
    return m;
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
    edge.arrowUpDown = app.isKeyDown(starforge::Key::Up);
    edge.arrowDownDown = app.isKeyDown(starforge::Key::Down);
    edge.arrowLeftDown = app.isKeyDown(starforge::Key::Left);
    edge.arrowRightDown = app.isKeyDown(starforge::Key::Right);

    tracker.prevW = w;
    tracker.prevS = s;
    tracker.prevSpace = space;
    tracker.prevPause = pause;

    const auto motion = app.takeMouseMotion();
    edge.mouseDx = motion.dx;
    edge.mouseDy = motion.dy;
    edge.mouseScrollY = motion.scrollY;
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

    auto nebula = starforge::Nebula::create(*renderer, {});
    if (!nebula)
    {
        logError(nebula.error().c_str());
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
    float cameraDist = 8.0f; // chase-camera distance (fixed for FOV zoom)
    float cameraFov = kPi / 3.0f; // vertical FOV radians (wheel zoom)

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
                if (auto nb = nebula->recreate(*renderer); !nb)
                {
                    logError(nb.error().c_str());
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
            pending.arrowUpDown = edge.arrowUpDown;
            pending.arrowDownDown = edge.arrowDownDown;
            pending.arrowLeftDown = edge.arrowLeftDown;
            pending.arrowRightDown = edge.arrowRightDown;
            pending.mouseDx += edge.mouseDx;
            pending.mouseDy += edge.mouseDy;
            pending.mouseScrollY += edge.mouseScrollY;
            pending.mouseLeftDown = edge.mouseLeftDown;
            pending.mouseRightDown = edge.mouseRightDown;
        }

        // Scroll-wheel FOV zoom (runs every frame, not step-gated).
        // Roll out (down) widens the view so you see more around you;
        // roll in (up) narrows it like a magnifier.
        if (pending.mouseScrollY != 0.0f)
        {
            const float before = cameraFov;
            cameraFov = std::clamp(cameraFov - pending.mouseScrollY * 0.02f, 0.4f, 1.7f);
            if (cameraFov != before)
            {
                std::printf("[INPUT] fov %.0f deg\n", cameraFov * 57.29578f);
                std::fflush(stdout);
            }
            pending.mouseScrollY = 0.0f;
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
            if (!flight.paused && pending.spacePressed)
            {
                flight.mode = flight.mode == starforge::game::FlightMode::Flight
                                  ? starforge::game::FlightMode::Ui
                                  : starforge::game::FlightMode::Flight;
                pending.spacePressed = false;
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
                pending.mouseScrollY = 0.0f;
                pending.mouseLeftDown = false;
                pending.mouseRightDown = false;
                accumulator = 0.0f;
            }
        }

        // --- Fixed-step simulation (M15.0.1) + streaming (M15.0.3) ---
        if (!flight.paused)
        {
            accumulator += frameDt;
        }
        if (const int loaded = world.updateStreaming(); loaded > 0)
        {
            std::printf("[STREAM] loaded %d chunk(s); mounted=%zu chunks=%zu\n", loaded,
                        world.mountedAsteroidCount(), world.streamedChunkCount());
            std::fflush(stdout);
        }
        int steps = 0;
        while (accumulator >= kSimStep && steps < 5 && !flight.paused) // cap: avoid spiral of death
        {
            const auto stepEdge = pending; // snapshot; edges already consumed
            starforge::game::applyInput(flight, stepEdge, kSimStep);

            const auto ship = world.shipTransform();
            const auto forward = forwardFromQuat(
                {ship.orientation.x, ship.orientation.y, ship.orientation.z, ship.orientation.w});
            // Zero the approach speed while overlapping a rock: the
            // controller rebuilds velocity each step, so bridge-side
            // damping alone would be overwritten next tick.
            const bool blocked = world.collideShip();
            const float speed = blocked ? 0.0f : starforge::game::targetSpeed(flight);
            world.setVelocity(
                {forward.x() * speed, forward.y() * speed, forward.z() * speed},
                {flight.pitchRate, flight.yawRate, flight.rollRate});
            world.step(kSimStep);
            if (blocked)
            {
                static float lastCollideLog = -10.0f;
                if (time - lastCollideLog >= 1.0f)
                {
                    std::printf("[COLLIDE] asteroid contact\n");
                    std::fflush(stdout);
                    lastCollideLog = time;
                }
            }
            accumulator -= kSimStep;
            ++steps;
            // Throttle edges are consumed deterministically on the
            // first step; levels (A/D, arrows, buttons, pad) persist.
            pending.wPressed = false;
            pending.sPressed = false;
            // Mouse deltas are consumed by the first step; held levels
            // (A/D roll, arrows, mouse buttons) must survive every sub-step.
            pending.mouseDx = 0.0f;
            pending.mouseDy = 0.0f;
            pending.mouseScrollY = 0.0f;
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
            const auto cell = world.shipCell();
            // World-anchored planet marker (~63 km from spawn).
            const double pdx = 1067000.0 - ship.position.x;
            const double pdy = 8000.0 - ship.position.y;
            const double pdz = -60000.0 - ship.position.z;
            const double planetDist = std::sqrt(pdx * pdx + pdy * pdy + pdz * pdz);
            std::printf(
                "[HUD] throttle %.0f%% speed %.0f m/s pos (%.0f, %.0f, %.0f) cell (%.0f,%.0f,%.0f) rocks %zu planet %.0f m mode %s%s\n",
                flight.throttle * 100.0f, effectiveSpeed, ship.position.x, ship.position.y,
                ship.position.z, cell.x, cell.y, cell.z, world.mountedAsteroidCount(), planetDist,
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
        // Camera-relative offsets (ship at origin); scroll wheel zooms.
        const starforge::math::Vec3f eyeOffset = {
            -shipForward.x() * cameraDist + shipUp.x() * (cameraDist * 0.31f),
            -shipForward.y() * cameraDist + shipUp.y() * (cameraDist * 0.31f),
            -shipForward.z() * cameraDist + shipUp.z() * (cameraDist * 0.31f)};
        const starforge::math::Vec3f lookAhead = {
            shipForward.x() * 10.0f, shipForward.y() * 10.0f, shipForward.z() * 10.0f};

        const float aspect = static_cast<float>(size.width) / std::max(static_cast<float>(size.height), 1.0f);
        const auto view = starforge::math::lookAt(eyeOffset, lookAhead, shipUp);
        // Far plane covers the world-anchored planet (~63 km); rocks live within 2.5 km.
        const auto proj = starforge::math::perspectiveVulkan(cameraFov, aspect, 0.1f, 150000.0f);
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
        // Sun is a distant-star direction (translation-invariant like
        // the starfield sky), deliberately in a different patch of sky
        // from the planet (~35° away) so the two read as separate
        // objects with sensible relative scale (star dominates).
        sunParams.worldPosition = {-5000.0f, 2500.0f, -6000.0f};
        sunParams.radius = 800.0f; // dominant disc (~11° at 8.6 km)
        sunParams.intensity = 1.0f;

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

        // --- Asteroid field (M15.0.3): streaming chunks + authored
        // near cluster, rendered as toon cubes (placeholder geometry).
        const auto rocks = world.visibleAsteroids(2500.0);
        std::vector<const starforge::game::RawTransform*> sorted;
        sorted.reserve(rocks.size());
        for (const auto& r : rocks)
        {
            sorted.push_back(&r);
        }
        constexpr std::size_t kMaxRocks = 64;
        const std::size_t take = std::min(sorted.size(), kMaxRocks);
        std::partial_sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(take), sorted.end(),
                          [](const starforge::game::RawTransform* a, const starforge::game::RawTransform* b) {
                              const double da = a->position.x * a->position.x + a->position.y * a->position.y +
                                                a->position.z * a->position.z;
                              const double db = b->position.x * b->position.x + b->position.y * b->position.y +
                                                b->position.z * b->position.z;
                              return da < db;
                          });

        // --- Environment showcase (M15.0.4) ---
        // Nebula backdrop first (fullscreen FBM), then stars, then
        // planet dish placeholder + ship + asteroids. Planet patch
        // renderer with atmosphere shell wiring is future 15.0.4 work
        // (documented placeholder until the GPU patch facade lands).
        starforge::Nebula::RenderContext nebContext;
        nebContext.viewProj = proj * view;
        nebContext.invViewProj = starforge::math::inverse(nebContext.viewProj);
        nebContext.cameraPosition = eyeOffset;
        nebContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
        nebContext.time = time;

        starforge::Nebula::Params nebParams;
        nebParams.baseColor = {0.30f, 0.15f, 0.50f, 1.0f};
        nebParams.density = 1.0f;
        nebParams.scattering = 0.45f;
        nebParams.time = time;

        // Planet dish placeholder: WORLD-anchored (~63 km from spawn),
        // computed ship-relative each frame like the sun used to be,
        // so it stays put in the world and grows as the ship approaches.
        // (Patch renderer + atmosphere shell remain future work.)
        constexpr starforge::game::RawVec3d kPlanetWorld{1067000.0, 8000.0, -60000.0};
        starforge::math::Vec3f planetRel{
            static_cast<float>(kPlanetWorld.x - ship.position.x),
            static_cast<float>(kPlanetWorld.y - ship.position.y),
            static_cast<float>(kPlanetWorld.z - ship.position.z)};
        starforge::math::Mat4f planetModel(1.0f);
        planetModel.columns[3] = {planetRel.x(), planetRel.y(), planetRel.z(), 1.0f};
        planetModel = planetModel * scaleMat(6000.0f);
        starforge::Toon::Context planetContext;
        planetContext.viewProj = proj * view * planetModel;
        planetContext.modelView = view * planetModel;
        planetContext.lightDirectionView = toonContext.lightDirectionView;
        starforge::Toon::Params planetParams;
        planetParams.baseColor = {0.15f, 0.45f, 1.0f, 1.0f}; // bright water blue
        planetParams.shadowColor = {0.02f, 0.12f, 0.35f, 1.0f};
        planetParams.bandCount = 4.0f;
        planetParams.rimStrength = 1.0f;
        planetParams.outlineWidth = 1.0f;

        // Composition order: starfield base (opaque), then nebula
        // additive over it, then planet/ship/rocks depth-tested.
        auto frame = renderer->renderFrame(starContext.clearColor, [&](starforge::FrameContext& fc) {
            starfield->record(fc, starContext, sunParams);
            nebula->record(fc, nebContext, nebParams);
            toon->record(fc, planetContext, planetParams);
            toon->record(fc, toonContext, toonParams);
            std::size_t shown = 0;
            for (const auto* rock : sorted)
            {
                if (shown >= kMaxRocks)
                {
                    break;
                }
                // Fade-in by distance (grow-in) masks streaming pop-in
                // at the 2.5 km cull radius.
                const double rdist = std::sqrt(rock->position.x * rock->position.x +
                                                 rock->position.y * rock->position.y +
                                                 rock->position.z * rock->position.z);
                float fade = 1.0f;
                if (rdist > 1800.0)
                {
                    fade = std::max(0.0f, 1.0f - static_cast<float>((rdist - 1800.0) / 700.0));
                }
                if (fade <= 0.001f)
                {
                    continue;
                }
                const starforge::math::Vec4f rq{
                    rock->orientation.x, rock->orientation.y, rock->orientation.z, rock->orientation.w};
                starforge::math::Mat4f rm(1.0f);
                rm.columns[3] = {static_cast<float>(rock->position.x), static_cast<float>(rock->position.y),
                                 static_cast<float>(rock->position.z), 1.0f};
                const auto rockModel = rm * matFromQuat(rq) * scaleMat(rock->scale * fade);
                starforge::Toon::Context rc;
                rc.viewProj = proj * view * rockModel;
                rc.modelView = view * rockModel;
                rc.lightDirectionView = toonContext.lightDirectionView;
                rc.resolution = toonContext.resolution;
                starforge::Toon::Params rp;
                rp.baseColor = {0.55f, 0.45f, 0.35f, 1.0f};
                rp.shadowColor = {0.15f, 0.12f, 0.1f, 1.0f};
                rp.bandCount = 2.0f;
                rp.rimStrength = 0.25f;
                rp.outlineWidth = 1.5f;
                toon->record(fc, rc, rp);
                ++shown;
            }
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
