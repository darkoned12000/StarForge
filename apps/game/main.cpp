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
#include "starforge/asteroid_field.hpp"
#include "starforge/audio.hpp"
#include "starforge/hud.hpp"
#include "starforge/math.hpp"
#include "starforge/nebula.hpp"
#include "starforge/planet.hpp"
#include "starforge/renderer.hpp"
#include "starforge/starfield.hpp"
#include "starforge/sun.hpp"
#include "starforge/toon.hpp"
#include "starforge/ui.hpp"
#include "starforge/world.hpp"

#include "game_bridge.hpp"
#include "game_simulation.hpp"
#include "ship_controller.hpp"
#include "targeting.hpp"

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

// Celestial sun (M15.0.7). One source of truth for the HUD and the
// renderer — the old demo had duplicated literals whose comments said
// ~80 km while the actual position was 469 km away.
//
// Numbers are chosen so the *whole* sun fits inside the camera far
// plane (150 km): centre 30 km from spawn, core 4 km, corona 3x = 12 km,
// i.e. 18..42 km from the ship. It is also reachable: 30 km at 60 m/s
// is ~8.5 minutes at full throttle. Direction is well away from the
// planet (~67 degrees) so the two read as separate objects.
constexpr starforge::game::RawVec3d kSunWorld{1047000.0 - 21000.0, 12000.0, -18000.0};
constexpr double kSunRadius = 4000.0;
constexpr double kSunCoronaMultiplier = 3.0;

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
    bool prevA = false;
    bool prevSpace = false;
    bool prevPause = false;
    bool prevUp = false;
    bool prevDown = false;
    bool prevLeft = false;
    bool prevRight = false;
    bool prevTab = false;
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
    tracker.prevA = app.isKeyDown(starforge::Key::A);
    tracker.prevSpace = space;
    tracker.prevPause = pause;
    tracker.prevUp = edge.arrowUpDown;
    tracker.prevDown = edge.arrowDownDown;
    tracker.prevLeft = edge.arrowLeftDown;
    tracker.prevRight = edge.arrowRightDown;

    const auto motion = app.takeMouseMotion();
    edge.mouseDx = motion.dx;
    edge.mouseDy = motion.dy;
    edge.mouseScrollY = motion.scrollY;
    edge.mouseLeftDown = app.isMouseButtonDown(starforge::MouseButton::Left);
    edge.mouseRightDown = app.isMouseButtonDown(starforge::MouseButton::Right);
    edge.boostDown = app.isKeyDown(starforge::Key::LeftShift);
    const bool tab = app.isKeyDown(starforge::Key::Tab);
    edge.tabPressed = tab && !tracker.prevTab;
    tracker.prevTab = tab;
    return edge;
}

// UI-mode trading keys (the public Key enum has no digits; arrows + W/S/A
// are reused only while in UI mode). Each is an edge, not a held level.
struct UiTracker
{
    bool up = false, down = false, left = false, right = false;
    bool w = false, s = false, a = false;
};

void handleUiActions(starforge::Application& app, UiTracker& tracker,
                     starforge::game::GameSimulation& simulation)
{
    const bool up = app.isKeyDown(starforge::Key::Up);
    const bool down = app.isKeyDown(starforge::Key::Down);
    const bool left = app.isKeyDown(starforge::Key::Left);
    const bool right = app.isKeyDown(starforge::Key::Right);
    const bool w = app.isKeyDown(starforge::Key::W);
    const bool s = app.isKeyDown(starforge::Key::S);
    const bool a = app.isKeyDown(starforge::Key::A);
    const bool dockEdge = up && !tracker.up;
    const bool undockEdge = down && !tracker.down;
    const bool buyEdge = left && !tracker.left;
    const bool sellEdge = right && !tracker.right;
    const bool acceptEdge = w && !tracker.w;
    const bool completeEdge = s && !tracker.s;
    const bool jumpEdge = a && !tracker.a;
    tracker.up = up; tracker.down = down; tracker.left = left; tracker.right = right;
    tracker.w = w; tracker.s = s; tracker.a = a;

    if (dockEdge)
    {
        simulation.updateDocking(20.0, 0.0, "station-a");
        if (simulation.dock("station-a", "slot-1")) logInfo("[TRADE] docked at station-a");
    }
    if (undockEdge)
    {
        simulation.undock();
        logInfo("[TRADE] undocked");
    }
    if (buyEdge)
    {
        if (simulation.buy("station-a", "ore", 5)) logInfo("[TRADE] bought 5 ore");
        else logError(simulation.lastTradeError().c_str());
    }
    if (sellEdge)
    {
        if (simulation.sell("station-a", "ore", 5)) logInfo("[TRADE] sold 5 ore");
        else logError(simulation.lastTradeError().c_str());
    }
    if (acceptEdge)
    {
        if (simulation.acceptMission("haul-ore")) logInfo("[MISSION] accepted haul-ore");
    }
    if (completeEdge)
    {
        if (simulation.deliverMission("haul-ore", "ore", 10, "station-b") &&
            simulation.completeMission("haul-ore", "station-b"))
            logInfo("[MISSION] haul-ore complete");
    }
    if (jumpEdge)
    {
        if (simulation.jump("alpha-centauri", 1.5f)) logInfo("[JUMP] entered alpha-centauri");
        else logError("[JUMP] beyond range - complete the haul mission first");
    }
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

    auto sun = starforge::Sun::create(*renderer, {});
    if (!sun)
    {
        logError(sun.error().c_str());
        return EXIT_FAILURE;
    }

    // M15.0.4: ray-sphere planet with an atmosphere limb.
    auto planet = starforge::Planet::create(*renderer, {});
    if (!planet)
    {
        logError(planet.error().c_str());
        return EXIT_FAILURE;
    }

    // M15.0.8/15.0.9: instanced lumpy rock field with tint + LOD + outline.
    auto rockField = starforge::AsteroidField::create(*renderer,
                                                      {.shaderDirectory = {}, .variantCount = 4});
    if (!rockField)
    {
        logError(rockField.error().c_str());
        return EXIT_FAILURE;
    }

    // M9.5 toon HUD draw + M12 audio for combat feedback.
    auto hud = starforge::Hud::create(*renderer, {});
    if (!hud)
    {
        logError(hud.error().c_str());
        return EXIT_FAILURE;
    }

    // M16: toon UI canvas for the flight HUD and target reticle.
    auto uiCanvas = starforge::UiCanvas::create(*renderer, {});
    if (!uiCanvas)
    {
        logError(uiCanvas.error().c_str());
        return EXIT_FAILURE;
    }
    // The facade defaults to the silent backend (for CI/headless tests), so
    // opt into real hardware here; fall back gracefully if no device exists.
    auto audio = starforge::Audio::create({
        .backend = starforge::Audio::CreateInfo::Backend::Default});
    if (!audio)
    {
        std::fprintf(stderr, "[STARFORGE] audio device unavailable (%s); continuing silent\n",
                     audio.error().c_str());
        audio = starforge::Audio::create({});
    }
    if (!audio)
    {
        logError(audio.error().c_str());
        return EXIT_FAILURE;
    }
    // Placeholder one-shots (no binary assets shipped).
    const auto laserWav = starforge::generateToneWav(22050, 1, 880.0f, 0.09f, 0.35f);
    const auto hitWav = starforge::generateToneWav(22050, 1, 220.0f, 0.16f, 0.5f);
    const auto boomWav = starforge::generateToneWav(22050, 1, 90.0f, 0.35f, 0.6f);
    const auto laserVoice = audio->loadFromMemory(laserWav);
    const auto hitVoice = audio->loadFromMemory(hitWav);
    const auto boomVoice = audio->loadFromMemory(boomWav);

    // Game layer: ECS world holds the player ship (raw bridge).
    starforge::game::GameWorld world;
    starforge::game::FlightState flight;
    starforge::game::GameSimulation simulation;
    starforge::game::TargetingState targeting;
    simulation.spawnHostile();
    logInfo("First Flight: ship + hostile + trading loop ready. W/S throttle, mouse steer, Space UI, P pause.");

    bool needRecreate = false;
    float time = 0.0f;
    float accumulator = 0.0f;
    float hudTimer = 0.0f;
    auto lastFrame = std::chrono::steady_clock::now();

    // Input carried across frames so taps and mouse deltas are never
    // lost on frames with zero fixed steps (display > 60 Hz).
    starforge::game::InputEdge pending;
    InputTracker inputTracker;
    UiTracker uiTracker;
    float lastThrottle = -1.0f;
    bool lastPaused = false;
    starforge::game::FlightMode lastMode = starforge::game::FlightMode::Flight;
    bool lastFirePrimary = false;
    bool lastFireSecondary = false;
    bool lastRelativeMouse = false;
    bool lastBoost = false;
    float cameraDist = 8.0f; // chase-camera distance (fixed for FOV zoom)
    float shieldFlash = 0.0f; // seconds of remaining shield-hit visual
    float explosionTimer = 0.0f; // hostile destruction effect
    starforge::game::RawVec3f explosionPos{};
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
                if (auto s = sun->recreate(*renderer); !s)
                {
                    logError(s.error().c_str());
                    return EXIT_FAILURE;
                }
                if (auto t = toon->recreate(*renderer); !t)
                {
                    logError(t.error().c_str());
                    return EXIT_FAILURE;
                }
                if (auto p = planet->recreate(*renderer); !p)
                {
                    logError(p.error().c_str());
                    return EXIT_FAILURE;
                }
                if (auto r = rockField->recreate(*renderer); !r)
                {
                    logError(r.error().c_str());
                    return EXIT_FAILURE;
                }
                if (auto h = hud->recreate(*renderer); !h)
                {
                    logError(h.error().c_str());
                    return EXIT_FAILURE;
                }
                uiCanvas = starforge::UiCanvas::create(*renderer, {});
                if (!uiCanvas)
                {
                    logError(uiCanvas.error().c_str());
                    return EXIT_FAILURE;
                }
            }
            needRecreate = false;
        }

        if (!flight.paused)
        {
            time += frameDt;
            shieldFlash = std::max(0.0f, shieldFlash - frameDt);
            explosionTimer = std::max(0.0f, explosionTimer - frameDt);
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
            pending.boostDown = edge.boostDown;
            pending.tabPressed = pending.tabPressed || edge.tabPressed;
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
            if (!flight.paused && pending.tabPressed)
            {
                targeting.cycleNext();
                pending.tabPressed = false;
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
                pending.boostDown = false;
                accumulator = 0.0f;
            }
        }

        // UI-mode trading / mission / jump actions.
        if (flight.mode == starforge::game::FlightMode::Ui && !flight.paused)
        {
            handleUiActions(*app, uiTracker, simulation);
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

            // M15 combat slice: advance hostile AI, weapons and damage.
            const starforge::game::RawVec3f shipForward{
                static_cast<float>(forward.x()), static_cast<float>(forward.y()),
                static_cast<float>(forward.z())};
            for (const auto& event : simulation.step(kSimStep, shipForward,
                                                     flight.firePrimary, flight.fireSecondary))
            {
                switch (event.type)
                {
                case starforge::game::GameEvent::Type::WeaponFired:
                    if (laserVoice.has_value()) audio->play(*laserVoice, true);
                    break;
                case starforge::game::GameEvent::Type::PlayerShieldHit:
                case starforge::game::GameEvent::Type::PlayerHullHit:
                    shieldFlash = 0.35f;
                    if (hitVoice.has_value()) audio->play(*hitVoice, true);
                    break;
                case starforge::game::GameEvent::Type::HostileDestroyed:
                    explosionTimer = 1.0f;
                    explosionPos = simulation.hostile().position;
                    if (boomVoice.has_value()) audio->play(*boomVoice, true);
                    logInfo("[COMBAT] hostile destroyed");
                    break;
                default:
                    break;
                }
            }
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
            if (flight.mode == starforge::game::FlightMode::Ui)
            {
                std::printf("[UI] Up=dock  Down=undock  Left=buy 5 ore  Right=sell 5 ore\n");
                std::printf("[UI] W=accept haul-ore  S=deliver+complete  A=jump (needs unlock)\n");
            }
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
        if (flight.boost != lastBoost)
        {
            std::printf("[INPUT] %s (%.0f m/s at full throttle)\n",
                        flight.boost ? "AFTERBURNER ON" : "AFTERBURNER OFF",
                        starforge::game::targetSpeed(flight));
            std::fflush(stdout);
            lastBoost = flight.boost;
        }
        lastFirePrimary = flight.firePrimary;
        lastFireSecondary = flight.fireSecondary;

        // M16.5: rebuild target contacts from live simulation state each frame.
        {
            const auto hostile = simulation.hostile();
            std::vector<starforge::game::TargetContact> contacts;
            if (hostile.alive)
            {
                const float dx = hostile.position.x, dy = hostile.position.y, dz = hostile.position.z;
                contacts.push_back({"hostile-1", true,
                                    std::sqrt(dx * dx + dy * dy + dz * dz),
                                    hostile.shield, hostile.hull});
            }
            contacts.push_back({"station-a", false, 1500.0f, 0.0f, 0.0f});
            targeting.setContacts(std::move(contacts));
            targeting.tick(frameDt, flight.firePrimary && !flight.paused);
        }

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

            // World-anchored celestial sun (~30 km from spawn).
            const double sdx = kSunWorld.x - ship.position.x;
            const double sdy = kSunWorld.y - ship.position.y;
            const double sdz = kSunWorld.z - ship.position.z;
            const double sunDist = std::sqrt(sdx * sdx + sdy * sdy + sdz * sdz);

            const auto hostileInfo = simulation.hostile();
            std::printf(
                "[HUD] throttle %.0f%% speed %.0f m/s cell (%.0f,%.0f,%.0f) rocks %zu planet %.0f m sun %.0f m mode %s%s\n",
                flight.throttle * 100.0f, effectiveSpeed, cell.x, cell.y, cell.z,
                world.mountedAsteroidCount(), planetDist, sunDist,
                flight.mode == starforge::game::FlightMode::Ui ? "UI" : "FLIGHT",
                flight.paused ? " PAUSED" : "");
            std::printf(
                "[M15] shield %.0f hull %.0f hostile %s (s%.0f/h%.0f) credits %d cargo %d system %s jump %.1fly missions %d%s\n",
                simulation.playerShield(), simulation.playerHull(),
                hostileInfo.alive ? (hostileInfo.engaging ? "ENGAGE" : "EVADE") : "DESTROYED",
                hostileInfo.shield, hostileInfo.hull, simulation.credits(), simulation.cargoUsed(),
                simulation.currentSystem().c_str(), simulation.jumpRangeLy(),
                simulation.completedMissions(), simulation.isDocked() ? " DOCKED" : "");
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
        // Far plane covers the world-anchored planet (~63 km), sun (~30 km
        // plus a 12 km corona) and rocks (within 2.5 km), with margin.
        const auto proj = starforge::math::perspectiveVulkan(cameraFov, aspect, 0.1f, 150000.0f);
        const auto viewProj = proj * view;
        const auto invViewProj = starforge::math::inverse(viewProj);
        // Ship sits at the ship-relative origin; lookAt already places it
        // 8 m ahead of the camera. Adding another translation would
        // double-shift it (review fix).
        const auto model = matFromQuat(shipQuat);

        starforge::Starfield::RenderContext starContext;
        starContext.viewProj = viewProj;
        starContext.invViewProj = invViewProj;
        starContext.time = time;
        if (flight.paused)
        {
            // Dark red tint while paused: visible without needing the console.
            starContext.clearColor = {.red = 0.25f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f};
        }

        // Flat billboard sun is disabled (intensity = 0.0f) in favor of the real 3D celestial sun (M15.0.7).
        starforge::Starfield::Params sunBillboardParams;
        sunBillboardParams.worldPosition = {-5000.0f, 2500.0f, -6000.0f};
        sunBillboardParams.radius = 800.0f;
        sunBillboardParams.intensity = 0.0f;   // billboard off; 3D sun is used instead
        sunBillboardParams.starIntensity = 1.0f; // stars remain visible

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

        // Hostile ship (M15.0.5): red hull with a fresnel shield shell.
        const auto hostileInfo = simulation.hostile();
        starforge::Toon::Context hostileContext;
        starforge::Toon::Params hostileParams;
        if (hostileInfo.alive)
        {
            starforge::math::Mat4f hm(1.0f);
            hm.columns[3] = {hostileInfo.position.x, hostileInfo.position.y,
                             hostileInfo.position.z, 1.0f};
            hostileContext.viewProj = viewProj * hm;
            hostileContext.modelView = view * hm;
            hostileContext.lightDirectionView = toonContext.lightDirectionView;
            hostileContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
            hostileParams.baseColor = {0.58f, 0.42f, 0.24f, 1.0f}; // brown NPC hull
            hostileParams.shadowColor = {0.22f, 0.14f, 0.07f, 1.0f};
            hostileParams.bandCount = 3.0f;
            hostileParams.rimStrength = 0.7f;
            hostileParams.outlineWidth = 2.0f;
            hostileParams.capabilities = hostileInfo.shield > 0.0f ? 2u : 0u; // fresnel shield
            hostileParams.emissiveColor = {0.2f, 0.05f, 0.5f, 0.6f};
        }

        // Visible combat feedback (M15.0.5): emissive muzzle flash while
        // firing and a fresnel shield bubble when the player is hit.
        starforge::Toon::Context muzzleContext;
        starforge::Toon::Params muzzleParams;
        if (flight.firePrimary)
        {
            auto mm = starforge::math::Mat4f(1.0f);
            mm.columns[0][0] = mm.columns[1][1] = mm.columns[2][2] = 0.45f;
            mm.columns[3] = {shipForward.x() * 8.0f, shipForward.y() * 8.0f,
                             shipForward.z() * 8.0f, 1.0f};
            muzzleContext.viewProj = viewProj * mm;
            muzzleContext.modelView = view * mm;
            muzzleContext.lightDirectionView = toonContext.lightDirectionView;
            muzzleContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
            muzzleParams.baseColor = {1.0f, 0.8f, 0.2f, 1.0f};
            muzzleParams.capabilities = 1u; // emissive
            muzzleParams.emissiveColor = {2.5f, 1.6f, 0.35f, 1.0f};
            muzzleParams.outlineWidth = 0.0f;
        }
        starforge::Toon::Context flashContext;
        starforge::Toon::Params flashParams;
        if (shieldFlash > 0.0f)
        {
            auto fm = starforge::math::Mat4f(1.0f);
            // Keep the bubble well inside the 8 m chase-camera distance.
            const float s = 1.2f + (0.35f - shieldFlash) * 1.8f;
            fm.columns[0][0] = fm.columns[1][1] = fm.columns[2][2] = s;
            flashContext.viewProj = viewProj * fm;
            flashContext.modelView = view * fm;
            flashContext.lightDirectionView = toonContext.lightDirectionView;
            flashContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
            flashParams.baseColor = {0.1f, 0.4f, 1.0f, 1.0f};
            flashParams.capabilities = 2u; // fresnel shield
            flashParams.emissiveColor = {0.2f, 0.6f, 1.0f, 0.7f};
            flashParams.outlineWidth = 0.0f;
        }

        // Stylized destruction burst: expanding emissive shells at the last
        // hostile position.
        starforge::Toon::Context boomContext;
        starforge::Toon::Params boomParams;
        if (explosionTimer > 0.0f)
        {
            const float progress = 1.0f - explosionTimer;
            auto bm = starforge::math::Mat4f(1.0f);
            const float s = 1.0f + progress * 7.0f;
            bm.columns[0][0] = bm.columns[1][1] = bm.columns[2][2] = s;
            bm.columns[3] = {explosionPos.x, explosionPos.y, explosionPos.z, 1.0f};
            boomContext.viewProj = viewProj * bm;
            boomContext.modelView = view * bm;
            boomContext.lightDirectionView = toonContext.lightDirectionView;
            boomContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
            boomParams.baseColor = {1.0f, 0.55f, 0.12f, 1.0f};
            boomParams.capabilities = 1u; // emissive
            boomParams.emissiveColor = {3.0f * explosionTimer, 1.2f * explosionTimer,
                                        0.2f * explosionTimer, 1.0f};
            boomParams.outlineWidth = 0.0f;
        }

        // --- Asteroid field (M15.0.3): streaming chunks + authored
        // near cluster, rendered as toon cubes (placeholder geometry).
        const auto visibleRocks = world.visibleAsteroids(2500.0);
        std::vector<const starforge::game::RawTransform*> sorted;
        sorted.reserve(visibleRocks.size());
        for (const auto& r : visibleRocks)
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
        // 3D visitable sun (M15.0.7), then planet dish placeholder + ship + asteroids.
        starforge::Nebula::RenderContext nebContext;
        nebContext.viewProj = viewProj;
        nebContext.invViewProj = invViewProj;
        nebContext.cameraPosition = eyeOffset;
        nebContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
        nebContext.time = time;

        starforge::Nebula::Params nebParams;
        nebParams.baseColor = {0.30f, 0.15f, 0.50f, 1.0f};
        nebParams.density = 1.0f;
        nebParams.scattering = 0.45f;
        nebParams.time = time;

        // --- 3D Visitable Celestial Sun (M15.0.7) ---
        // Real 3D celestial mesh with M7 quadtree LOD (LargeObjectSetup),
        // Eddington limb darkening (1 - u(1 - μ)), and additive corona shell.
        // Anchored at world coordinates (~30 km from spawn); the core and
        // corona both sit inside the far plane so the whole body renders.
        starforge::Sun::RenderContext sunContext;
        sunContext.viewProj = viewProj;
        sunContext.cameraPosition = starforge::WorldPosition::fromMeters(
            ship.position.x + eyeOffset.x(),
            ship.position.y + eyeOffset.y(),
            ship.position.z + eyeOffset.z()).value_or(starforge::WorldPosition{});
        sunContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
        sunContext.time = time;

        starforge::Sun::Params sun3DParams;
        sun3DParams.worldPosition = starforge::WorldPosition::fromMeters(
            kSunWorld.x, kSunWorld.y, kSunWorld.z).value_or(starforge::WorldPosition{});
        sun3DParams.radius = kSunRadius; // 4 km celestial photosphere radius
        sun3DParams.coronaRadiusMultiplier = kSunCoronaMultiplier; // Corona reaches out to 12 km
        sun3DParams.coreColor = {1.0f, 0.95f, 0.82f, 1.0f}; // Blazing yellow-white core
        sun3DParams.limbColor = {0.95f, 0.40f, 0.05f, 1.0f}; // Deep amber solar rim
        sun3DParams.coronaColor = {1.0f, 0.65f, 0.20f, 1.0f}; // Glowing outer corona
        sun3DParams.limbDarkeningCoeff = 0.6f;
        sun3DParams.coronaIntensity = 1.0f;
        sun3DParams.time = time;

        // M15.0.4 planet: WORLD-anchored (~63 km from spawn), computed
        // ship-relative each frame, rendered as a ray-sphere disc with a
        // projective depth write and a Fresnel atmosphere limb.
        constexpr starforge::game::RawVec3d kPlanetWorld{1067000.0, 8000.0, -60000.0};
        starforge::math::Vec3f planetRel{
            static_cast<float>(kPlanetWorld.x - ship.position.x),
            static_cast<float>(kPlanetWorld.y - ship.position.y),
            static_cast<float>(kPlanetWorld.z - ship.position.z)};
        starforge::Planet::RenderContext planetContext;
        planetContext.viewProj = viewProj;
        planetContext.invViewProj = invViewProj;
        planetContext.cameraPosition = eyeOffset;
        planetContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
        planetContext.time = time;
        starforge::Planet::Params planetParams;
        planetParams.center = planetRel;
        planetParams.radius = 6000.0f;
        planetParams.baseColor = {0.18f, 0.5f, 1.0f, 1.0f};
        planetParams.shadowColor = {0.12f, 0.35f, 0.65f, 1.0f}; // clearly opaque night side
        planetParams.rimColor = {0.6f, 0.8f, 1.0f, 1.0f};
        // Light direction must be planet->sun (not ship->sun), otherwise the
        // terminator points the wrong way because the two bodies are far apart.
        planetParams.sunDirection = starforge::math::normalize(
            starforge::math::Vec3f{kSunWorld.x - kPlanetWorld.x,
                                   kSunWorld.y - kPlanetWorld.y,
                                   kSunWorld.z - kPlanetWorld.z});
        planetParams.fresnelPower = 3.5f;
        planetParams.rimStrength = 1.2f;
        planetParams.atmosphereRatio = 1.18f;
        planetParams.atmosphereColor = {0.35f, 0.6f, 1.0f, 1.0f};
        // The additive limb read as a second, hollow planet at some angles,
        // so it is disabled in the demo; the disc alone is unmistakably solid.
        planetParams.atmosphereIntensity = 0.0f;
        // ~250 s per revolution (halved from 0.05 rad/s).
        planetParams.spin = time * 0.025f;

        // M15.0.8/15.0.9 instanced asteroid field: build camera-relative
        // instances with a palette tint keyed by the rock seed/position.
        starforge::AsteroidField::RenderContext rockContext;
        rockContext.viewProj = viewProj;
        rockContext.projectionScale = {proj.columns[0][0], proj.columns[1][1]};
        rockContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
        // Rock normals are in ship-relative world space (not view space), so
        // pass the real direction to the sun; backlit faces go dark.
        rockContext.sunDirection = starforge::math::normalize(
            starforge::math::Vec3f{kSunWorld.x - ship.position.x,
                                   kSunWorld.y - ship.position.y,
                                   kSunWorld.z - ship.position.z});
        rockContext.cameraPosition = eyeOffset;
        rockContext.lodDistance = 1600.0f;
        rockContext.outlineWidthPixels = 1.2f;
        rockContext.outline = true;
        std::vector<starforge::AsteroidField::Instance> rockInstances;
        rockInstances.reserve(sorted.size());
        for (const auto* rock : sorted)
        {
            const double rdist = std::sqrt(rock->position.x * rock->position.x +
                                           rock->position.y * rock->position.y +
                                           rock->position.z * rock->position.z);
            if (rdist > 2500.0) break;
            // Stable per-entity seed: never derived from camera-relative
            // position, which changes as the ship flies and caused flicker.
            const int palette = static_cast<int>(rock->seed % 4);
            const float tints[4][3] = {
                {0.72f, 0.55f, 0.30f}, // tan
                {0.35f, 0.52f, 0.72f}, // slate blue
                {0.70f, 0.32f, 0.24f}, // rust
                {0.38f, 0.62f, 0.35f}}; // sage
            starforge::AsteroidField::Instance instance;
            instance.position = {static_cast<float>(rock->position.x),
                                 static_cast<float>(rock->position.y),
                                 static_cast<float>(rock->position.z)};
            instance.scale = rock->scale;
            instance.rotation = {rock->orientation.x, rock->orientation.y,
                                 rock->orientation.z, rock->orientation.w};
            instance.tint = {tints[palette][0], tints[palette][1], tints[palette][2], 1.0f};
            instance.seed = rock->seed; // stable shape/tint across frames
            rockInstances.push_back(instance);
        }
        starforge::Hud::Params hudParams;
        hudParams.panelX = 16.0f;
        hudParams.panelY = 16.0f;
        hudParams.panelWidth = 200.0f;
        hudParams.panelHeight = 96.0f;
        hudParams.portraitX = 28.0f;
        hudParams.portraitY = 28.0f;
        hudParams.portraitWidth = 56.0f;
        hudParams.portraitHeight = 56.0f;
        hudParams.panelColor = {0.06f, 0.10f, 0.20f, 1.0f};
        hudParams.outlineColor = {0.02f, 0.02f, 0.04f, 1.0f};
        hudParams.accentColor = {0.95f, 0.65f, 0.15f, 1.0f};
        // Live readouts: shield / hull / cargo.
        const float flightBars[3] = {
            std::clamp(simulation.playerShield() / 100.0f, 0.0f, 1.0f),
            std::clamp(simulation.playerHull() / 100.0f, 0.0f, 1.0f),
            std::clamp(static_cast<float>(simulation.cargoUsed()) / 30.0f, 0.0f, 1.0f)};
        std::copy_n(flightBars, 3, hudParams.barValues);

        // UI mode: a larger trade/combat panel with status bars.
        starforge::Hud::Params uiParams = hudParams;
        uiParams.panelX = static_cast<float>(size.width) * 0.5f - 240.0f;
        uiParams.panelY = static_cast<float>(size.height) * 0.5f - 160.0f;
        uiParams.panelWidth = 480.0f;
        uiParams.panelHeight = 320.0f;
        uiParams.portraitX = uiParams.panelX + 20.0f;
        uiParams.portraitY = uiParams.panelY + 20.0f;
        uiParams.portraitWidth = 96.0f;
        uiParams.portraitHeight = 96.0f;
        uiParams.panelColor = {0.08f, 0.14f, 0.26f, 1.0f};
        uiParams.accentColor = {0.35f, 0.85f, 0.55f, 1.0f};
        // Credits / cargo / completed missions.
        const float uiBars[3] = {
            std::clamp(static_cast<float>(simulation.credits()) / 2000.0f, 0.0f, 1.0f),
            std::clamp(static_cast<float>(simulation.cargoUsed()) / 30.0f, 0.0f, 1.0f),
            std::clamp(static_cast<float>(simulation.completedMissions()) / 2.0f, 0.0f, 1.0f)};
        std::copy_n(uiBars, 3, uiParams.barValues);

        // Composition order: starfield base (opaque stars), then nebula
        // additive over it, then 3D celestial sun (opaque core + additive corona),
        // then planet/ship/rocks depth-tested.
        auto frame = renderer->renderFrame(starContext.clearColor, [&](starforge::FrameContext& fc) {
            starfield->record(fc, starContext, sunBillboardParams);
            nebula->record(fc, nebContext, nebParams);
            sun->record(fc, sunContext, sun3DParams);
            planet->record(fc, planetContext, planetParams);
            toon->record(fc, toonContext, toonParams);
            if (hostileInfo.alive) toon->record(fc, hostileContext, hostileParams);
            if (flight.firePrimary) toon->record(fc, muzzleContext, muzzleParams);
            if (shieldFlash > 0.0f) toon->record(fc, flashContext, flashParams);
            if (explosionTimer > 0.0f) toon->record(fc, boomContext, boomParams);
            // One instanced draw (+ outline + impostor tiers) replaces the
            // old 64 per-rock Toon draws (M15.0.9).
            rockField->record(fc, rockContext, rockInstances.data(),
                              static_cast<std::uint32_t>(rockInstances.size()));

            // M16.5 flight HUD + targeting reticle (toon canvas, one draw).
            uiCanvas->clear();
            const float hudScale = 3.0f;
            const float cx = static_cast<float>(size.width) * 0.5f;
            const float cy = static_cast<float>(size.height) * 0.5f;
            uiCanvas->addPanel({.x = 16.0f, .y = 16.0f, .w = 320.0f, .h = 176.0f,
                                .outlineWidth = 2.0f,
                                .fillColor = {0.05f, 0.09f, 0.18f, 0.85f}});
            const auto hudText = [&](float tx, float ty, const char* label, float value) {
                char buffer[48];
                std::snprintf(buffer, sizeof(buffer), "%s %.0f", label, value);
                uiCanvas->addText({.x = tx, .y = ty, .scale = hudScale,
                                   .color = {0.85f, 0.95f, 1.0f, 1.0f}, .text = buffer});
            };
            hudText(28.0f, 26.0f, "THROTTLE", flight.throttle * 100.0f);
            hudText(28.0f, 50.0f, "SPEED", starforge::game::targetSpeed(flight));
            hudText(28.0f, 74.0f, "SHIELD", simulation.playerShield());
            hudText(28.0f, 98.0f, "HULL", simulation.playerHull());
            hudText(28.0f, 122.0f, "CREDITS", static_cast<float>(simulation.credits()));
            hudText(28.0f, 146.0f, "CARGO", static_cast<float>(simulation.cargoUsed()));
            uiCanvas->addBar({.x = 150.0f, .y = 82.0f, .w = 170.0f, .h = 10.0f,
                              .value = simulation.playerShield() / 100.0f,
                              .fillColor = {0.2f, 0.6f, 1.0f, 1.0f}});
            uiCanvas->addBar({.x = 150.0f, .y = 106.0f, .w = 170.0f, .h = 10.0f,
                              .value = simulation.playerHull() / 100.0f,
                              .fillColor = {0.2f, 0.9f, 0.4f, 1.0f}});
            // Crosshair: four short strokes around screen centre.
            const auto reticle = starforge::math::Vec4f{1.0f, 1.0f, 1.0f, 0.9f};
            uiCanvas->addRect({.x = cx - 18.0f, .y = cy - 1.0f, .w = 10.0f, .h = 2.0f, .color = reticle});
            uiCanvas->addRect({.x = cx + 8.0f, .y = cy - 1.0f, .w = 10.0f, .h = 2.0f, .color = reticle});
            uiCanvas->addRect({.x = cx - 1.0f, .y = cy - 18.0f, .w = 2.0f, .h = 10.0f, .color = reticle});
            uiCanvas->addRect({.x = cx - 1.0f, .y = cy + 8.0f, .w = 2.0f, .h = 10.0f, .color = reticle});
            // Selected target bracket + lock bar.
            if (const auto* target = targeting.selected())
            {
                const starforge::math::Vec4f tc = target->hostile
                    ? starforge::math::Vec4f{1.0f, 0.3f, 0.2f, 1.0f}
                    : starforge::math::Vec4f{0.3f, 0.8f, 1.0f, 1.0f};
                const float bx = cx - 30.0f, by = cy - 30.0f;
                uiCanvas->addRect({.x = bx, .y = by, .w = 12.0f, .h = 2.0f, .color = tc});
                uiCanvas->addRect({.x = bx + 48.0f, .y = by, .w = 12.0f, .h = 2.0f, .color = tc});
                uiCanvas->addRect({.x = bx, .y = by, .w = 2.0f, .h = 12.0f, .color = tc});
                uiCanvas->addRect({.x = bx + 58.0f, .y = by, .w = 2.0f, .h = 12.0f, .color = tc});
                uiCanvas->addText({.x = cx - 60.0f, .y = cy + 36.0f, .scale = 2.0f, .color = tc,
                                   .text = target->id});
                if (targeting.lockProgress() > 0.0f)
                    uiCanvas->addBar({.x = cx - 40.0f, .y = cy + 56.0f, .w = 80.0f, .h = 6.0f,
                                      .value = targeting.lockProgress(), .fillColor = tc});
            }
            uiCanvas->record(fc);

            hud->record(fc, hudParams);
            if (flight.mode == starforge::game::FlightMode::Ui)
                hud->record(fc, uiParams);
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
