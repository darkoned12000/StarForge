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
#include "starforge/asset.hpp"
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
#include "ui_comms.hpp"
#include "ui_radar.hpp"
#include "ui_map.hpp"
#include "ui_menu.hpp"
#include "ui_station.hpp"
#include "ui_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifndef STARFORGE_ASSET_DIR
#define STARFORGE_ASSET_DIR "assets/models"
#endif

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
// M17: station is anchored in world space, so flying changes your range to it
// instead of dragging it along in front of the ship.
// Ship spawns at kSpawnX (1047000 m); the outpost sits just ahead of it.
constexpr starforge::game::RawVec3d kStationWorld{1047040.0, 20.0, -450.0};
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

// Compact low-poly fighter used for sector NPCs (nose = -Z). Built once into
// a renderer-owned Toon mesh; hostile/neutral colour is a per-draw param.
[[nodiscard]] std::expected<starforge::Toon::ToonMesh, std::string>
makeNpcFighterMesh(starforge::Toon& toon)
{
    using starforge::math::Vec3f;
    std::vector<starforge::Toon::MeshVertex> vertices;
    auto triangle = [&](Vec3f a, Vec3f b, Vec3f c) {
        const Vec3f ab{b.x() - a.x(), b.y() - a.y(), b.z() - a.z()};
        const Vec3f ac{c.x() - a.x(), c.y() - a.y(), c.z() - a.z()};
        Vec3f n{ab.y() * ac.z() - ab.z() * ac.y(),
                ab.z() * ac.x() - ab.x() * ac.z(),
                ab.x() * ac.y() - ab.y() * ac.x()};
        n = starforge::math::normalize(n);
        for (const auto& p : {a, b, c})
            vertices.push_back({p, n, n});
    };

    const Vec3f nose{0.0f, 0.0f, -1.5f}, tail{0.0f, 0.0f, 0.95f};
    const Vec3f top{0.0f, 0.34f, 0.15f}, bottom{0.0f, -0.24f, 0.15f};
    const Vec3f left{-0.52f, 0.0f, 0.25f}, right{0.52f, 0.0f, 0.25f};
    const Vec3f tipL{-1.55f, -0.04f, 0.70f}, tipR{1.55f, -0.04f, 0.70f};
    // Tapered fuselage.
    triangle(nose, top, left);
    triangle(nose, right, top);
    triangle(nose, left, bottom);
    triangle(nose, bottom, right);
    triangle(tail, left, top);
    triangle(tail, top, right);
    triangle(tail, bottom, left);
    triangle(tail, right, bottom);
    // Swept delta wings with a slight dihedral.
    triangle(left, tipL, tail);
    triangle(right, tail, tipR);
    triangle(left, tail, tipL);
    triangle(right, tipR, tail);
    return toon.createMesh(vertices);
}

// Low-poly sphere used as the shield-hit bubble (the built-in cube read as a
// solid blue block). Rendered through the additive glow pass.
[[nodiscard]] std::expected<starforge::Toon::ToonMesh, std::string>
makeShieldMesh(starforge::Toon& toon)
{
    using starforge::math::Vec3f;
    std::vector<starforge::Toon::MeshVertex> vertices;
    constexpr int kRings = 8, kSegments = 16;
    auto point = [](int ring, int segment) {
        const float phi = static_cast<float>(ring) / kRings * 3.14159265f;
        const float theta = static_cast<float>(segment) / kSegments * 6.2831853f;
        const Vec3f n{std::sin(phi) * std::cos(theta), std::cos(phi),
                      std::sin(phi) * std::sin(theta)};
        return std::pair{n, n};
    };
    auto push = [&](int ring, int segment) {
        const auto [n, o] = point(ring, segment);
        vertices.push_back({n, n, o});
    };
    for (int r = 0; r < kRings; ++r)
        for (int s = 0; s < kSegments; ++s)
        {
            push(r, s);
            push(r + 1, s);
            push(r + 1, s + 1);
            push(r, s);
            push(r + 1, s + 1);
            push(r, s + 1);
        }
    return toon.createMesh(vertices);
}

// M17: authored glTF model with per-part LODs, streamed mips and toon
// material mapping. Everything here uses the public API only.
struct AuthoredPart
{
    std::vector<starforge::Toon::ToonMesh> lods;
    starforge::ToonMaterialDesc material;
    starforge::StreamingTexture texture;
    std::size_t activeMip = 0;
};

struct AuthoredModel
{
    std::string path;
    std::unique_ptr<starforge::Asset> asset;
    std::vector<AuthoredPart> parts;
    starforge::AssetLodSelector selector{std::vector<float>{}};
    std::size_t activeLod = 0;
    bool loaded = false;
};

[[nodiscard]] starforge::Toon::Params
makeToonParams(const starforge::ToonMaterialDesc& desc, starforge::MaterialVariant variant)
{
    const auto mapped = starforge::applyAssetMaterialVariant(desc, variant);
    starforge::Toon::Params params;
    params.baseColor = mapped.baseColor;
    params.shadowColor = mapped.shadowColor;
    params.midColor = mapped.midColor;
    params.bandCount = mapped.bandCount;
    params.rimStrength = mapped.rimStrength;
    params.outlineWidth = mapped.outlineWidth;
    params.capabilities = mapped.capabilities;
    params.emissiveColor = mapped.emissiveColor;
    params.fresnelColor = mapped.fresnelColor;
    params.materialParams = {mapped.fresnelPower, 0.0f, 0.0f, 0.0f};
    return params;
}

// Loads an authored GLB and builds GPU meshes for it. The `screenPixels`
// hint picks the streamed mip used for the base-colour texture.
[[nodiscard]] bool loadAuthoredModel(starforge::Toon& toon, const std::string& path,
                                     float screenPixels, AuthoredModel& out, std::string& error)
{
    auto asset = starforge::Asset::loadFromFile(path);
    if (!asset) { error = asset.error(); return false; }
    out.path = path;
    out.asset = std::make_unique<starforge::Asset>(std::move(*asset));
    out.parts.clear();
    out.selector = starforge::AssetLodSelector(
        std::vector<float>{250.0f, 1000.0f, 1.0e9f});
    out.activeLod = 0;

    for (const auto& mesh : out.asset->meshes())
    {
        auto levels = starforge::generateAssetLods(mesh, std::vector<float>{0.5f, 0.25f});
        if (!levels) { error = levels.error(); return false; }

        AuthoredPart part;
        part.material = starforge::mapAssetMaterial(mesh.material);
        if (mesh.material.hasBaseColorTexture && !mesh.material.textureRGBA.empty())
        {
            auto texture = starforge::StreamingTexture::fromRGBA(
                mesh.material.textureWidth, mesh.material.textureHeight,
                mesh.material.textureRGBA);
            if (texture)
            {
                part.activeMip = texture->selectMip(static_cast<std::uint32_t>(screenPixels));
                (void)texture->request(part.activeMip);
                part.texture = std::move(*texture);
            }
        }
        // Flat emissive parts (nav lights / beacons) have no albedo texture;
        // feed the mip overload a 1x1 white pixel so it still uploads.
        static constexpr std::uint8_t kWhitePixel[4] = {255, 255, 255, 255};
        for (const auto& level : *levels)
        {
            const auto pixels = part.texture.mipCount() > 0
                ? part.texture.data(part.activeMip)
                : std::span<const std::uint8_t>(kWhitePixel, 4u);
            const auto width = part.texture.mipCount() > 0
                ? part.texture.width(part.activeMip) : 1u;
            const auto height = part.texture.mipCount() > 0
                ? part.texture.height(part.activeMip) : 1u;
            auto gpu = toon.createMeshFromAsset(level, pixels, width, height);
            if (!gpu) { error = gpu.error(); return false; }
            part.lods.push_back(std::move(*gpu));
        }
        out.parts.push_back(std::move(part));
    }
    out.loaded = !out.parts.empty();
    return out.loaded;
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
    bool prevReturn = false;
    bool prevBackspace = false;
    bool prevZoomIn = false;
    bool prevZoomOut = false;
    bool prevMap = false;
    bool prevLock = false;
    bool prevEscape = false;
    bool prevPageUp = false;
    bool prevPageDown = false;
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
                     starforge::game::GameSimulation& simulation,
                     starforge::game::CommsLog& comms,
                     double stationDistance = 20.0)
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
        simulation.updateDocking(stationDistance, 0.0, "station-a");
        if (simulation.dock("station-a", "slot-1"))
        {
            logInfo("[TRADE] docked at station-a");
            comms.push(starforge::game::CommsChannel::Trade, "station-a", "Docking granted");
        }
    }
    if (undockEdge)
    {
        simulation.undock();
        logInfo("[TRADE] undocked");
    }
    if (buyEdge)
    {
        if (simulation.buy("station-a", "ore", 5))
        {
            logInfo("[TRADE] bought 5 ore");
            comms.push(starforge::game::CommsChannel::Trade, "station-a", "Bought 5 ore");
        }
        else
        {
            logError(simulation.lastTradeError().c_str());
            comms.push(starforge::game::CommsChannel::System, "system", simulation.lastTradeError());
        }
    }
    if (sellEdge)
    {
        if (simulation.sell("station-a", "ore", 5))
        {
            logInfo("[TRADE] sold 5 ore");
            comms.push(starforge::game::CommsChannel::Trade, "station-a", "Sold 5 ore");
        }
        else
        {
            logError(simulation.lastTradeError().c_str());
            comms.push(starforge::game::CommsChannel::System, "system", simulation.lastTradeError());
        }
    }
    if (acceptEdge)
    {
        if (simulation.acceptMission("haul-ore"))
        {
            logInfo("[MISSION] accepted haul-ore");
            comms.push(starforge::game::CommsChannel::System, "missions", "Contract accepted: Ore Haul");
        }
    }
    if (completeEdge)
    {
        if (simulation.deliverMission("haul-ore", "ore", 10, "station-b") &&
            simulation.completeMission("haul-ore", "station-b"))
        {
            logInfo("[MISSION] haul-ore complete");
            comms.push(starforge::game::CommsChannel::System, "missions", "Ore Haul complete: +500 cr");
        }
        else
            comms.push(starforge::game::CommsChannel::System, "missions",
                       "Accept the haul and carry 10 ore to station-b first");
    }
    if (jumpEdge)
    {
        const float distance = starforge::game::systemDistanceLy(
            simulation.currentSystem(), "alpha-centauri");
        if (distance > 0.0f && simulation.jump("alpha-centauri", distance))
        {
            logInfo("[JUMP] entered alpha-centauri");
            comms.push(starforge::game::CommsChannel::System, "nav", "Jumped to alpha-centauri");
        }
        else
        {
            logError("[JUMP] beyond range");
            comms.push(starforge::game::CommsChannel::System, "nav", "Jump range insufficient");
        }
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

    // M17: load authored hero ship + station meshes through the asset facade.
    AuthoredModel shipModel;
    AuthoredModel stationModel;
    starforge::AssetWatcher assetWatcher;
    std::string assetError;
    const std::string shipPath = std::string(STARFORGE_ASSET_DIR) + "/hero_ship.glb";
    const std::string stationPath = std::string(STARFORGE_ASSET_DIR) + "/station.glb";
    if (!loadAuthoredModel(*toon, shipPath, 1024.0f, shipModel, assetError))
        logError(("[ASSET] hero ship: " + assetError).c_str());
    if (!loadAuthoredModel(*toon, stationPath, 512.0f, stationModel, assetError))
        logError(("[ASSET] station: " + assetError).c_str());
    if (shipModel.loaded) assetWatcher.watch(shipPath);
    if (stationModel.loaded) assetWatcher.watch(stationPath);
    logInfo(("[ASSET] loaded " + std::to_string(shipModel.parts.size()) +
             " ship parts, " + std::to_string(stationModel.parts.size()) +
             " station parts").c_str());
    auto npcFighter = makeNpcFighterMesh(*toon);
    if (!npcFighter)
        logError(("[ASSET] NPC fighter mesh: " + npcFighter.error()).c_str());
    auto shieldMesh = makeShieldMesh(*toon);
    if (!shieldMesh)
        logError(("[ASSET] shield mesh: " + shieldMesh.error()).c_str());

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
    // Comms keeps a permanent scrollable history; toasts expire on their TTL.
    starforge::game::CommsLog comms{false};
    starforge::game::CommsLog toasts{true};
    starforge::game::TextEntry textEntry;
    bool chatActive = false;
    // Station interaction is opt-in via the proximity prompt; the old Space
    // toggle no longer opens the trade screen.
    bool stationOpen = false;
    enum class StationTab { Trade, Missions };
    StationTab stationTab = StationTab::Trade;
    double stationRangeMetres = 1.0e9;
    starforge::game::UiState uiState;
    // M16 polish: spawn a set of travelling NPCs (hostiles + neutrals) so
    // targeting can be exercised against several moving contacts.
    simulation.spawnNpcs(5);
    logInfo("First Flight: ship + 5 NPCs + trading loop ready. W/S throttle, mouse steer, T lock/untarget, Space UI, P pause.");
    // Resolve an NPC's last known position for combat effects (the NPC stays
    // in the list after death, so its position is still available here).
    auto npcPositionById = [&simulation](const std::string& id) -> starforge::game::RawVec3f {
        for (const auto& npc : simulation.npcs())
            if (npc.id == id)
                return npc.position;
        return {};
    };

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
    float radarRange = 2000.0f;
    bool mapVisible = false;
    bool quitRequested = false;
    std::string selectedSystem = "sol";
    float cameraDist = 8.0f; // chase-camera distance (fixed for FOV zoom)
    float shieldFlash = 0.0f; // seconds of remaining shield-hit visual
    float explosionTimer = 0.0f; // hostile destruction effect
    starforge::game::RawVec3f explosionPos{};
    float cameraFov = kPi / 3.0f; // vertical FOV radians (wheel zoom)

    // M17 polish: short-lived visual laser bolts (camera-relative world space,
    // fired along the reticle ray). Gameplay damage is resolved by
    // GameSimulation; these are purely cosmetic tracers.
    struct LaserBolt
    {
        starforge::math::Vec3f position{};
        starforge::math::Vec3f direction{0.0f, 0.0f, -1.0f};
        float life = 0.0f;
    };
    std::vector<LaserBolt> laserBolts;
    float boltCooldown = 0.0f;
    // Incoming enemy fire tracers (ship-local space, attacker -> player).
    struct IncomingShot
    {
        starforge::math::Vec3f from{};
        float life = 0.0f;
    };
    std::vector<IncomingShot> incomingShots;
    struct Fragment
    {
        starforge::math::Vec3f position{};
        float scale = 1.0f;
        float life = 0.0f;
    };
    std::vector<Fragment> fragments;

    while (!app->shouldClose() && !quitRequested)
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

        // Station range for the proximity prompt (world-anchored outpost).
        {
            const auto shipPos = world.shipTransform().position;
            stationRangeMetres = std::sqrt(
                (kStationWorld.x - shipPos.x) * (kStationWorld.x - shipPos.x) +
                (kStationWorld.y - shipPos.y) * (kStationWorld.y - shipPos.y) +
                (kStationWorld.z - shipPos.z) * (kStationWorld.z - shipPos.z));
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
            if (pending.pausePressed && !chatActive)
            {
                flight.paused = !flight.paused;
                pending.pausePressed = false;
            }
            if (!flight.paused && !chatActive && pending.tabPressed)
            {
                targeting.cycleNext();
                pending.tabPressed = false;
            }
            // Radar zoom on +/- edges (both flight and UI modes).
            {
                const bool zoomIn = app->isKeyDown(starforge::Key::Equal);
                const bool zoomOut = app->isKeyDown(starforge::Key::Minus);
                if (zoomIn && !inputTracker.prevZoomIn)
                    radarRange = std::max(250.0f, radarRange * 0.75f);
                if (zoomOut && !inputTracker.prevZoomOut)
                    radarRange = std::min(8000.0f, radarRange * 1.333f);
                inputTracker.prevZoomIn = zoomIn;
                inputTracker.prevZoomOut = zoomOut;
                const bool mapKey = app->isKeyDown(starforge::Key::M);
                if (mapKey && !inputTracker.prevMap)
                {
                    mapVisible = !mapVisible;
                    if (mapVisible)
                    {
                        // Free the cursor so the map is immediately clickable.
                        flight.paused = false;
                        flight.mode = starforge::game::FlightMode::Ui;
                    }
                }
                inputTracker.prevMap = mapKey;
                const bool lockKey = app->isKeyDown(starforge::Key::T);
                if (lockKey && !inputTracker.prevLock)
                {
                    targeting.toggleLock();
                    logInfo(targeting.hasSelection() ? "[TGT] locked nearest contact"
                                                      : "[TGT] target cleared");
                }
                inputTracker.prevLock = lockKey;
            }
            // Space frees the cursor so UI buttons (including the proximity
            // dock/chat prompt) can be clicked. It does not open the station
            // panel directly - that still requires the <DOCK> prompt.
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

        // One hit-test frame shared by the pause menu, station screen and
        // sector map. Calling beginFrame once keeps every widget's hover /
        // click state alive for the whole frame (including rendering).
        {
            const auto cursor = app->mousePosition();
            uiState.beginFrame(cursor.x, cursor.y,
                               app->mouseButtonPressed(starforge::MouseButton::Left));
        }

        // Proximity station prompt: register its two buttons before any other
        // UI so the prompt is clickable while flying (no Space toggle).
        if (!flight.paused && !chatActive && !stationOpen &&
            stationRangeMetres < 1000.0)
        {
            const auto fb = app->framebufferSize();
            const auto prompt = starforge::game::stationPromptRect(
                static_cast<float>(fb.width), static_cast<float>(fb.height));
            uiState.addWidget("prompt:dock", prompt.dock);
            uiState.addWidget("prompt:chat", prompt.chat);
            if (uiState.clicked("prompt:dock"))
            {
                stationOpen = true;
                flight.mode = starforge::game::FlightMode::Ui;
                comms.push(starforge::game::CommsChannel::System, "station-a",
                           "Docking session opened");
            }
            if (uiState.clicked("prompt:chat"))
            {
                chatActive = true;
                textEntry.begin(">");
            }
        }
        if (stationOpen && !flight.paused)
            flight.mode = starforge::game::FlightMode::Ui;

        // Pause menu: clickable Resume / Map / Quit buttons.
        if (flight.paused && !chatActive)
        {
            const auto fb = app->framebufferSize();
            const starforge::game::UiRect panel{fb.width * 0.5f - 110.0f,
                                                fb.height * 0.5f - 80.0f, 220.0f, 240.0f};
            for (const auto& button : starforge::game::menuButtons())
                uiState.addWidget(button.id,
                                  starforge::game::UiRect{panel.x + button.x,
                                                          panel.y + button.y,
                                                          button.w, button.h});
            for (const auto& button : starforge::game::menuButtons())
            {
                if (!uiState.clicked(button.id)) continue;
                switch (starforge::game::menuActionFor(button.id))
                {
                case starforge::game::MenuAction::Resume:
                    flight.paused = false;
                    break;
                case starforge::game::MenuAction::ToggleMap:
                    // Opening the map from the menu closes the menu and enters
                    // UI mode so the cursor is free and the map is immediately
                    // interactive - no need to dismiss the menu first.
                    mapVisible = true;
                    flight.paused = false;
                    flight.mode = starforge::game::FlightMode::Ui;
                    break;
                case starforge::game::MenuAction::Quit:
                    quitRequested = true;
                    break;
                default:
                    break;
                }
            }
        }

        // --- Comms chat composer (works in BOTH flight and UI modes) ---
        // Enter toggles the composer open/closed; type, Enter sends to sector
        // traffic and Escape cancels. PageUp/PageDown scroll the history.
        {
            const std::string typed = app->takeTextInput();
            const bool enter = app->isKeyDown(starforge::Key::Return);
            const bool escape = app->isKeyDown(starforge::Key::Escape);
            const bool backspace = app->isKeyDown(starforge::Key::Backspace);
            const bool pageUp = app->isKeyDown(starforge::Key::PageUp);
            const bool pageDown = app->isKeyDown(starforge::Key::PageDown);

            if (enter && !inputTracker.prevReturn)
            {
                if (chatActive)
                {
                    const std::string line = textEntry.submit();
                    if (!line.empty())
                    {
                        comms.push(starforge::game::CommsChannel::Local, "you", line);
                        logInfo(("[CHAT] " + line).c_str());
                        // Canned reply from a nearby NPC until chat AI is wired.
                        for (const auto& npc : simulation.npcs())
                            if (npc.alive)
                            {
                                comms.push(starforge::game::CommsChannel::Local, npc.id,
                                           starforge::game::npcReply(npc.id));
                                break;
                            }
                    }
                    chatActive = false;
                    textEntry.end();
                }
                else
                {
                    chatActive = true;
                    textEntry.begin(">");
                }
            }
            if (escape && !inputTracker.prevEscape)
            {
                if (chatActive)
                {
                    chatActive = false;
                    textEntry.end();
                }
                else if (stationOpen)
                {
                    stationOpen = false;
                    flight.mode = starforge::game::FlightMode::Flight;
                }
            }
            if (chatActive)
            {
                textEntry.append(typed);
                if (backspace && !inputTracker.prevBackspace) textEntry.backspace();
            }

            if (pageUp && !inputTracker.prevPageUp) comms.scrollBack(3);
            if (pageDown && !inputTracker.prevPageDown) comms.scrollForward(3);

            inputTracker.prevReturn = enter;
            inputTracker.prevEscape = escape;
            inputTracker.prevBackspace = backspace;
            inputTracker.prevPageUp = pageUp;
            inputTracker.prevPageDown = pageDown;
        }
        if (chatActive)
        {
            // Typing letters must not steer the ship or fire weapons.
            pending.wPressed = false;
            pending.sPressed = false;
            pending.aDown = false;
            pending.dDown = false;
            pending.spacePressed = false;
            pending.pausePressed = false;
            pending.mouseDx = 0.0f;
            pending.mouseDy = 0.0f;
        }

        // Station trading / mission actions (only while docked at the prompt).
        if (stationOpen && flight.mode == starforge::game::FlightMode::Ui &&
            !flight.paused && !chatActive)
        {
            const auto stationShip = world.shipTransform().position;
            const double stationRange = std::sqrt(
                (kStationWorld.x - stationShip.x) * (kStationWorld.x - stationShip.x) +
                (kStationWorld.y - stationShip.y) * (kStationWorld.y - stationShip.y) +
                (kStationWorld.z - stationShip.z) * (kStationWorld.z - stationShip.z));
            // M16.8: clickable station screen. Register widgets in the same
            // geometry the renderer uses, then apply any clicked action.
            {
                const auto fb = app->framebufferSize();
                const auto panel = starforge::game::stationPanelRect(
                    static_cast<float>(fb.width), static_cast<float>(fb.height));
                uiState.addWidget("tab:trade",
                                  starforge::game::UiRect{panel.x + 18.0f, panel.y + 50.0f,
                                                          150.0f, 28.0f});
                uiState.addWidget("tab:missions",
                                  starforge::game::UiRect{panel.x + 176.0f, panel.y + 50.0f,
                                                          150.0f, 28.0f});
                if (uiState.clicked("tab:trade")) stationTab = StationTab::Trade;
                if (uiState.clicked("tab:missions")) stationTab = StationTab::Missions;
                for (const auto& button : starforge::game::stationButtons())
                {
                    // "dock" is intentionally omitted: the Trading view is
                    // only reachable after docking, so a DOCK button is noise.
                    const bool onTrade = button.id == "buy-5" || button.id == "sell-5" ||
                                         button.id == "undock";
                    const bool always = button.id == "close";
                    if (!always && (stationTab == StationTab::Trade ? !onTrade : onTrade))
                        continue;
                    uiState.addWidget(button.id,
                                      starforge::game::UiRect{panel.x + button.x, panel.y + button.y,
                                                              button.w, button.h});
                }
                for (const auto& button : starforge::game::stationButtons())
                {
                    if (!uiState.clicked(button.id)) continue;
                    if (button.id == "close" || button.id == "undock")
                    {
                        stationOpen = false;
                        flight.mode = starforge::game::FlightMode::Flight;
                        comms.push(starforge::game::CommsChannel::Trade, "station-a",
                                   "Undocked - safe travels");
                        continue;
                    }
                    starforge::game::applyStationAction(button.id, simulation, comms,
                                                        stationRange);
                }
            }
            handleUiActions(*app, uiTracker, simulation, comms, stationRange);
        }

        // Interactive sector map: click a system to select it, then JUMP TO
        // <name> if it is within range. Runs whenever the map is open so a
        // map opened from the pause menu is interactive immediately.
        if (mapVisible && !flight.paused && !chatActive)
        {
            const auto fb = app->framebufferSize();
            const auto layout = starforge::game::mapLayout(
                static_cast<float>(fb.width), static_cast<float>(fb.height),
                simulation.currentSystem(), simulation.jumpRangeLy());
            for (const auto& sys : layout.systems)
                uiState.addWidget("map:" + sys.id, sys.rect);
            uiState.addWidget("map-jump", layout.jumpButton);
            for (const auto& sys : layout.systems)
                if (uiState.clicked("map:" + sys.id))
                {
                    selectedSystem = sys.id;
                    comms.push(starforge::game::CommsChannel::System, "nav",
                               "Selected " + selectedSystem);
                }
            if (uiState.clicked("map-jump"))
            {
                const auto found = std::find_if(layout.systems.begin(), layout.systems.end(),
                    [&](const auto& s) { return s.id == selectedSystem; });
                if (found != layout.systems.end() && found->point.reachable)
                {
                    if (simulation.jump(selectedSystem, found->point.distance))
                        comms.push(starforge::game::CommsChannel::System, "nav",
                                   "Jumped to " + selectedSystem);
                }
                else
                    comms.push(starforge::game::CommsChannel::System, "nav",
                               "System out of jump range");
            }
        }

        comms.tick(frameDt);
        toasts.tick(frameDt);

        // M17: poll authored assets and hot-reload any that changed on disk.
        for (const auto& changed : assetWatcher.poll())
        {
            std::string reloadError;
            const bool isShip = changed == shipPath;
            auto& target = isShip ? shipModel : stationModel;
            if (loadAuthoredModel(*toon, changed, isShip ? 1024.0f : 512.0f,
                                  target, reloadError))
            {
                comms.push(starforge::game::CommsChannel::System, "assets",
                           std::string("Hot-reloaded ") + (isShip ? "hero ship" : "station"));
                logInfo(("[ASSET] hot-reloaded " + changed).c_str());
            }
            else
                logError(("[ASSET] reload failed: " + reloadError).c_str());
        }

        // M17 polish: cosmetic tracers. Bolts live in camera-relative WORLD
        // space and travel down the camera's optical axis, so the visible
        // beam runs through the reticle instead of the ship's nose. The chase
        // camera sits above the hull, so ship-forward rays used to appear to
        // pass underneath a target centred in the crosshair.
        if (!flight.paused)
        {
            const auto boltShip = world.shipTransform();
            const auto boltQuat = starforge::math::Vec4f{
                boltShip.orientation.x, boltShip.orientation.y,
                boltShip.orientation.z, boltShip.orientation.w};
            const auto boltFwd = toVec3f(forwardFromQuat(boltQuat));
            const auto boltUp = toVec3f(upFromQuat(boltQuat));
            // Match the render camera: eye behind/above the ship, looking at a
            // point 10 m ahead at hull height. The optical axis is the reticle.
            const auto boltEye = boltFwd * (-cameraDist) + boltUp * (0.31f * cameraDist);
            const auto boltAim = starforge::math::normalize(
                boltFwd * (10.0f + cameraDist) + boltUp * (-0.31f * cameraDist));
            // Spawn on that axis near the hull: both tracers start by the ship
            // and then travel exactly through screen centre at every range.
            const auto boltOrigin = boltEye + boltAim * (cameraDist + 2.0f);
            const auto boltBasis = matFromQuat(boltQuat);
            const auto boltRight = starforge::math::Vec3f{
                boltBasis.columns[0][0], boltBasis.columns[0][1], boltBasis.columns[0][2]};
            boltCooldown = std::max(0.0f, boltCooldown - frameDt);
            if (flight.firePrimary && boltCooldown <= 0.0f)
            {
                boltCooldown = 0.12f;
                for (const float bx : {-0.28f, 0.28f})
                    laserBolts.push_back({boltOrigin + boltRight * bx, boltAim, 1.2f});
            }
            // Test each tracer against nearby asteroids (both in world space).
            const auto rocksInRange = world.visibleAsteroids(600.0);
            for (auto& bolt : laserBolts)
            {
                bolt.position = bolt.position + bolt.direction * (600.0f * frameDt);
                bolt.life -= frameDt;
                for (const auto& rock : rocksInRange)
                {
                    const float dx = static_cast<float>(rock.position.x) - bolt.position.x();
                    const float dy = static_cast<float>(rock.position.y) - bolt.position.y();
                    const float dz = static_cast<float>(rock.position.z) - bolt.position.z();
                    const float radius = rock.scale * 0.866f + 2.0f;
                    if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
                    starforge::game::RawVec3d impact{};
                    float rockScale = 0.0f;
                    const bool destroyed = world.damageAsteroid(
                        rock.seed, 12.0f, &impact, &rockScale);
                    fragments.push_back({
                        {static_cast<float>(impact.x), static_cast<float>(impact.y),
                         static_cast<float>(impact.z)},
                        destroyed ? rockScale * 0.4f : 0.5f, 0.45f});
                    bolt.life = 0.0f;
                    break;
                }
            }
            laserBolts.erase(
                std::remove_if(laserBolts.begin(), laserBolts.end(),
                               [](const LaserBolt& b) { return b.life <= 0.0f; }),
                laserBolts.end());
            for (auto& fragment : fragments) fragment.life -= frameDt;
            fragments.erase(
                std::remove_if(fragments.begin(), fragments.end(),
                               [](const Fragment& f) { return f.life <= 0.0f; }),
                fragments.end());
            for (auto& shot : incomingShots) shot.life -= frameDt;
            incomingShots.erase(
                std::remove_if(incomingShots.begin(), incomingShots.end(),
                               [](const IncomingShot& s) { return s.life <= 0.0f; }),
                incomingShots.end());
        }

        // M17: pick the station LOD from the real ship-to-station range.
        if (stationModel.loaded)
        {
            const auto shipForLod = world.shipTransform().position;
            const float stationRange = static_cast<float>(std::sqrt(
                (kStationWorld.x - shipForLod.x) * (kStationWorld.x - shipForLod.x) +
                (kStationWorld.y - shipForLod.y) * (kStationWorld.y - shipForLod.y) +
                (kStationWorld.z - shipForLod.z) * (kStationWorld.z - shipForLod.z)));
            stationModel.activeLod = stationModel.selector.update(stationRange, 40.0f);
        }

        // M17: streamed mip follows the camera zoom (FOV). Rebuild GPU
        // meshes only when the chosen mip actually changes.
        for (auto* model : {&shipModel, &stationModel})
        {
            if (!model->loaded) continue;
            const auto* firstPart = &model->parts.front();
            if (firstPart->texture.mipCount() == 0) continue;
            // Close-up ships should use the full-resolution ink texture;
            // a 512 px footprint estimate keeps 256 px textures on mip 0.
            const auto wanted = firstPart->texture.selectMip(
                static_cast<std::uint32_t>(512.0f / std::max(0.4f, cameraFov)));
            if (wanted == firstPart->activeMip) continue;
            for (auto& part : model->parts)
                if (part.texture.mipCount() > 0) (void)part.texture.request(wanted);
            if (loadAuthoredModel(*toon, model->path,
                                  512.0f / std::max(0.4f, cameraFov), *model, assetError))
                firstPart = &model->parts.front();
            else
                logError(("[ASSET] mip stream failed: " + assetError).c_str());
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
                    toasts.push(starforge::game::CommsChannel::Combat, "", "SHIELD HIT", 2.0f);
                    // Show the incoming tracer from the NPC that fired.
                    for (const auto& attacker : simulation.npcs())
                        if (attacker.alive && attacker.id == event.sourceId)
                        {
                            incomingShots.push_back({
                                {attacker.position.x, attacker.position.y, attacker.position.z},
                                0.45f});
                            break;
                        }
                    if (hitVoice.has_value()) audio->play(*hitVoice, true);
                    break;
                case starforge::game::GameEvent::Type::HostileDestroyed:
                    comms.push(starforge::game::CommsChannel::Combat, "combat",
                               (event.sourceId.empty() ? std::string("NPC")
                                                       : event.sourceId + " destroyed"));
                    toasts.push(starforge::game::CommsChannel::Combat, "", "TARGET DESTROYED", 3.0f);
                    explosionTimer = 1.0f;
                    explosionPos = npcPositionById(event.sourceId);
                    if (boomVoice.has_value()) audio->play(*boomVoice, true);
                    logInfo(("[COMBAT] NPC destroyed: " + event.sourceId).c_str());
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
                std::printf("[UI] Enter=toggle chat, PgUp/PgDn=comms history\n");
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

        // M16.5: rebuild target contacts from every live NPC each frame.
        {
            std::vector<starforge::game::TargetContact> contacts;
            for (const auto& npc : simulation.npcs())
            {
                if (!npc.alive) continue;
                const float dx = npc.position.x, dy = npc.position.y, dz = npc.position.z;
                contacts.push_back({npc.id, npc.hostile,
                                    std::sqrt(dx * dx + dy * dy + dz * dz),
                                    npc.shield, npc.hull});
            }
            contacts.push_back({"station-a", false, 1500.0f, 0.0f, 0.0f});
            targeting.setContacts(std::move(contacts));
            targeting.tick(frameDt, flight.firePrimary && !flight.paused);
            // Point the ship's weapons at whatever the player has selected.
            const auto* selectedContact = targeting.selected();
            simulation.setTargetId(selectedContact ? selectedContact->id : std::string{});
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

            std::size_t aliveNpcs = 0, aliveHostiles = 0;
            for (const auto& npc : simulation.npcs())
                if (npc.alive) { ++aliveNpcs; if (npc.hostile) ++aliveHostiles; }
            std::printf(
                "[HUD] throttle %.0f%% speed %.0f m/s cell (%.0f,%.0f,%.0f) rocks %zu planet %.0f m sun %.0f m mode %s%s\n",
                flight.throttle * 100.0f, effectiveSpeed, cell.x, cell.y, cell.z,
                world.mountedAsteroidCount(), planetDist, sunDist,
                flight.mode == starforge::game::FlightMode::Ui ? "UI" : "FLIGHT",
                flight.paused ? " PAUSED" : "");
            std::printf(
                "[M15] shield %.0f hull %.0f NPCs %zu/%zu (hostiles %zu) target %s credits %d cargo %d system %s jump %.1fly missions %d%s\n",
                simulation.playerShield(), simulation.playerHull(),
                aliveNpcs, simulation.npcs().size(), aliveHostiles,
                targeting.hasSelection() ? targeting.selected()->id.c_str() : "NONE",
                simulation.credits(), simulation.cargoUsed(),
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
        // M16 polish: each system gets a distinct look so jumps are visible.
        const auto visual = starforge::game::systemVisual(simulation.currentSystem());
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
        starContext.clearColor = {.red = visual.nebulaColor[0] * 0.25f,
                                  .green = visual.nebulaColor[1] * 0.25f,
                                  .blue = visual.nebulaColor[2] * 0.25f,
                                  .alpha = 1.0f};
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
        // Ship uses a fixed SHIP-LOCAL key light transformed by its full
        // modelView, so its cel shading does not slide as the ship turns.
        // (World-anchored objects like the station use the real sun instead.)
        const auto shipLight4 = (view * model) * starforge::math::Vec4f{
            0.4f, 0.5f, 0.75f, 0.0f};
        toonContext.lightDirectionView = starforge::math::normalize(
            starforge::math::Vec3f{shipLight4.x(), shipLight4.y(), shipLight4.z()});

        starforge::Toon::Params toonParams;
        toonParams.baseColor = {0.85f, 0.25f, 0.2f, 1.0f};
        toonParams.shadowColor = {0.2f, 0.05f, 0.3f, 1.0f};
        toonParams.bandCount = 3.0f;
        toonParams.rimStrength = 0.6f;
        toonParams.outlineWidth = 2.0f;

        // M16 polish: render every live NPC. Hostiles keep the brown/red
        // hull with a shield shell; neutral traders use a cooler cyan hull.
        struct NpcRender
        {
            starforge::Toon::Context context;
            starforge::Toon::Params params;
        };
        std::vector<NpcRender> npcRenders;
        for (const auto& npc : simulation.npcs())
        {
            if (!npc.alive) continue;
            // Orient the fighter toward the player so it reads as a ship,
            // not an axis-aligned box. Nose is -Z in object space.
            const starforge::math::Vec3f toward = starforge::math::normalize(
                starforge::math::Vec3f{-npc.position.x, -npc.position.y, -npc.position.z});
            const starforge::math::Vec3f worldUp{0.0f, 1.0f, 0.0f};
            starforge::math::Vec3f right = starforge::math::cross(toward, worldUp);
            right = starforge::math::length(right) > 1e-4f
                ? starforge::math::normalize(right) : starforge::math::Vec3f{1.0f, 0.0f, 0.0f};
            const auto correctedUp = starforge::math::cross(right, toward);
            constexpr float kNpcScale = 6.0f; // make small craft readable at range
            starforge::math::Mat4f hm(1.0f);
            hm.columns[0] = {right.x() * kNpcScale, right.y() * kNpcScale,
                             right.z() * kNpcScale, 0.0f};
            hm.columns[1] = {correctedUp.x() * kNpcScale, correctedUp.y() * kNpcScale,
                             correctedUp.z() * kNpcScale, 0.0f};
            hm.columns[2] = {-toward.x() * kNpcScale, -toward.y() * kNpcScale,
                             -toward.z() * kNpcScale, 0.0f};
            hm.columns[3] = {npc.position.x, npc.position.y, npc.position.z, 1.0f};
            NpcRender r;
            r.context.viewProj = viewProj * hm;
            r.context.modelView = view * hm;
            // Ship-local key light keeps NPC colours stable as they manoeuvre.
            const auto npcLight4 = (view * hm) * starforge::math::Vec4f{
                0.4f, 0.5f, 0.75f, 0.0f};
            r.context.lightDirectionView = starforge::math::normalize(
                starforge::math::Vec3f{npcLight4.x(), npcLight4.y(), npcLight4.z()});
            r.context.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
            if (npc.hostile)
            {
                r.params.baseColor = {1.0f, 0.38f, 0.20f, 1.0f}; // bright hostile hull
                r.params.shadowColor = {0.35f, 0.10f, 0.05f, 1.0f};
                r.params.emissiveColor = {0.2f, 0.05f, 0.5f, 0.6f};
            }
            else
            {
                r.params.baseColor = {0.30f, 0.95f, 1.0f, 1.0f}; // bright neutral hull
                r.params.shadowColor = {0.06f, 0.25f, 0.30f, 1.0f};
                r.params.emissiveColor = {0.05f, 0.2f, 0.28f, 0.4f};
            }
            r.params.bandCount = 3.0f;
            r.params.rimStrength = 0.7f;
            r.params.outlineWidth = 1.5f;
            r.params.capabilities = npc.shield > 0.0f ? 2u : 0u; // fresnel shield
            r.params.fresnelColor = {0.35f, 0.8f, 1.0f, 0.45f};
            npcRenders.push_back(std::move(r));
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
            const float s = 2.1f + (0.35f - shieldFlash) * 1.2f;
            fm.columns[0][0] = fm.columns[1][1] = fm.columns[2][2] = s;
            flashContext.viewProj = viewProj * fm;
            flashContext.modelView = view * fm;
            flashContext.lightDirectionView = toonContext.lightDirectionView;
            flashContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
            // Additive glow on a spherical mesh: a soft cyan shell rather
            // than an opaque blue cube occluding the ship.
            const float strength = std::clamp(shieldFlash / 0.35f, 0.0f, 1.0f);
            // Zero every diffuse term: only the faint emissive shell shows.
            flashParams.baseColor = {0.0f, 0.0f, 0.0f, 1.0f};
            flashParams.shadowColor = {0.0f, 0.0f, 0.0f, 1.0f};
            flashParams.midColor = {0.0f, 0.0f, 0.0f, 0.0f};
            flashParams.rimStrength = 0.0f;
            flashParams.bandCount = 2.0f;
            flashParams.capabilities = 1u; // additive glow, no depth write
            flashParams.emissiveColor = {0.10f, 0.42f, 1.0f, 0.03f + 0.10f * strength};
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
        nebParams.baseColor = {visual.nebulaColor[0], visual.nebulaColor[1],
                               visual.nebulaColor[2], visual.nebulaColor[3]};
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
        sun3DParams.coreColor = {visual.sunCore[0], visual.sunCore[1],
                                 visual.sunCore[2], visual.sunCore[3]};
        sun3DParams.limbColor = {visual.sunLimb[0], visual.sunLimb[1],
                                 visual.sunLimb[2], visual.sunLimb[3]};
        sun3DParams.coronaColor = {visual.sunCorona[0], visual.sunCorona[1],
                                   visual.sunCorona[2], visual.sunCorona[3]};
        sun3DParams.limbDarkeningCoeff = 0.6f;
        sun3DParams.coronaIntensity = visual.coronaIntensity;
        sun3DParams.time = time;

        // M15.0.4 planet: WORLD-anchored (~63 km from spawn), computed
        // ship-relative each frame, rendered as a ray-sphere disc with a
        // projective depth write and a Fresnel atmosphere limb.
        constexpr starforge::game::RawVec3d kPlanetWorld{1067000.0, 8000.0, -60000.0};
        starforge::math::Vec3f planetRel{
            static_cast<float>(kPlanetWorld.x - ship.position.x),
            static_cast<float>(kPlanetWorld.y - ship.position.y),
            static_cast<float>(kPlanetWorld.z - ship.position.z)};
        const starforge::math::Vec3f stationRel{
            static_cast<float>(kStationWorld.x - ship.position.x),
            static_cast<float>(kStationWorld.y - ship.position.y),
            static_cast<float>(kStationWorld.z - ship.position.z)};
        starforge::Planet::RenderContext planetContext;
        planetContext.viewProj = viewProj;
        planetContext.invViewProj = invViewProj;
        planetContext.cameraPosition = eyeOffset;
        planetContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
        planetContext.time = time;
        starforge::Planet::Params planetParams;
        planetParams.center = planetRel;
        planetParams.radius = visual.planetRadius;
        planetParams.baseColor = {visual.planetBase[0], visual.planetBase[1],
                                  visual.planetBase[2], visual.planetBase[3]};
        planetParams.shadowColor = {visual.planetShadow[0], visual.planetShadow[1],
                                    visual.planetShadow[2], visual.planetShadow[3]};
        planetParams.rimColor = {visual.planetRim[0], visual.planetRim[1],
                                 visual.planetRim[2], visual.planetRim[3]};
        // Light direction must be planet->sun (not ship->sun), otherwise the
        // terminator points the wrong way because the two bodies are far apart.
        planetParams.sunDirection = starforge::math::normalize(
            starforge::math::Vec3f{kSunWorld.x - kPlanetWorld.x,
                                   kSunWorld.y - kPlanetWorld.y,
                                   kSunWorld.z - kPlanetWorld.z});
        planetParams.fresnelPower = 3.5f;
        planetParams.rimStrength = 0.45f;
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
            // M17: authored hero ship (material variant + streamed mip + LOD).
            if (shipModel.loaded)
            {
                auto variant = starforge::MaterialVariant::Default;
                if (simulation.playerHull() < 50.0f)
                    variant = starforge::MaterialVariant::Damaged;
                else if (simulation.playerShield() < 25.0f)
                    variant = starforge::MaterialVariant::Shielded;
                // Targeting (T) is purely a HUD/UI concern: never recolour the
                // ship just because a target was locked.
                for (const auto& part : shipModel.parts)
                {
                    const auto& mesh = part.lods[shipModel.activeLod];
                    toon->recordMesh(fc, toonContext, makeToonParams(part.material, variant), mesh);
                }
            }
            else
                toon->record(fc, toonContext, toonParams);

            // M17: authored station at a fixed sector position.
            if (stationModel.loaded)
            {
                starforge::math::Mat4f stationModel4(1.0f);
                // Bring the outpost into view: scaled up and ahead of the
                // ship instead of a speck 1.2 km off to the side.
                constexpr float kStationScale = 10.0f;
                stationModel4.columns[0][0] = kStationScale;
                stationModel4.columns[1][1] = kStationScale;
                stationModel4.columns[2][2] = kStationScale;
                stationModel4.columns[3] = {stationRel.x(), stationRel.y(), stationRel.z(), 1.0f};
                starforge::Toon::Context stationContext;
                stationContext.viewProj = viewProj * stationModel4;
                stationContext.modelView = view * stationModel4;
                // World sun rotated into view space: stable for a world-anchored
                // object regardless of which way the ship is pointing.
                const starforge::math::Vec3f stationSunWorld = starforge::math::normalize(
                    starforge::math::Vec3f{kSunWorld.x - ship.position.x,
                                           kSunWorld.y - ship.position.y,
                                           kSunWorld.z - ship.position.z});
                const auto stationSun4 = view * starforge::math::Vec4f{
                    stationSunWorld.x(), stationSunWorld.y(), stationSunWorld.z(), 0.0f};
                stationContext.lightDirectionView = starforge::math::normalize(
                    starforge::math::Vec3f{stationSun4.x(), stationSun4.y(), stationSun4.z()});
                stationContext.resolution = toonContext.resolution;
                for (const auto& part : stationModel.parts)
                {
                    const auto& mesh = part.lods[stationModel.activeLod];
                    auto params = makeToonParams(part.material,
                                                 starforge::MaterialVariant::Default);
                    if (part.material.name == "station-beacons")
                    {
                        // Smooth red navigation flash. The glTF material is
                        // emissive and texture-free, so all six faces glow and
                        // the pulse reads from any angle at 800-1200 m.
                        const float pulse = 0.5f + 0.5f * std::sin(time * 2.3f);
                        const float blink = 0.12f + 0.88f * pulse * pulse;
                        params.baseColor = {1.0f, 0.10f, 0.06f, 1.0f};
                        params.emissiveColor = {1.0f, 0.07f, 0.03f, blink * 1.8f};
                        params.outlineWidth = 0.0f;
                    }
                    toon->recordMesh(fc, stationContext, params, mesh);
                }
            }
            // M17 polish: thrusters follow throttle level. One press of W/S
            // raises throttle to 25%, which shows a small persistent plume;
            // it grows with power and disappears entirely at 0%.
            const bool thrustersEngaged = !flight.paused && flight.throttle > 0.01f;
            if (thrustersEngaged)
            {
                const float power = std::clamp(flight.throttle, 0.0f, 1.0f);
                auto drawGlow = [&](float px, float py, float pz,
                                    float sx, float sy, float sz,
                                    float r, float g, float b, float intensity) {
                    auto transform = starforge::math::Mat4f(1.0f);
                    transform.columns[0][0] = sx;
                    transform.columns[1][1] = sy;
                    transform.columns[2][2] = sz;
                    transform.columns[3] = {px, py, pz, 1.0f};
                    starforge::Toon::Context glowContext;
                    glowContext.viewProj = viewProj * model * transform;
                    glowContext.modelView = view * model * transform;
                    glowContext.lightDirectionView = toonContext.lightDirectionView;
                    glowContext.resolution = toonContext.resolution;
                    starforge::Toon::Params glowParams;
                    glowParams.capabilities = 1u; // additive glow
                    glowParams.baseColor = {0.02f, 0.05f, 0.10f, 1.0f};
                    glowParams.emissiveColor = {r, g, b, intensity};
                    glowParams.outlineWidth = 0.0f;
                    toon->record(fc, glowContext, glowParams);
                };

                // plume scale grows with throttle: short at 25%, long at 100%.
                const float plumeLength = 0.35f + power * 2.2f;
                const float plumeBrightness = 0.35f + power * 0.75f;
                for (const float ex : {-0.42f, 0.42f})
                {
                    const float flicker = 0.92f + 0.08f * std::sin(time * 27.0f);
                    drawGlow(ex, 0.02f, 2.12f, 0.38f, 0.38f, 0.24f,
                              0.03f, 0.32f, 1.0f, plumeBrightness * flicker);
                    constexpr int kSegments = 10;
                    for (int segment = 0; segment < kSegments; ++segment)
                    {
                        const float t = static_cast<float>(segment) / (kSegments - 1);
                        const float width = (0.38f * (1.0f - t) + 0.05f) * (0.6f + 0.4f * power);
                        const float length = plumeLength / kSegments + 0.08f;
                        const float z = 2.20f + static_cast<float>(segment) * (plumeLength / kSegments);
                        const float intensity = (0.55f * (1.0f - t) + 0.02f) * plumeBrightness * flicker;
                        drawGlow(ex, 0.02f, z, width, width, length,
                                  0.02f + 0.03f * t, 0.22f + 0.10f * t, 1.0f, intensity);
                    }
                }
            }
            // Cyan player tracers, oriented along their actual flight vector.
            auto orientBeam = [](starforge::math::Mat4f& matrix,
                                 const starforge::math::Vec3f& direction,
                                 const starforge::math::Vec3f& position,
                                 float length) {
                const auto fwd = starforge::math::normalize(direction);
                auto right = starforge::math::cross(fwd, starforge::math::Vec3f{0.0f, 1.0f, 0.0f});
                right = starforge::math::length(right) > 1e-4f
                    ? starforge::math::normalize(right) : starforge::math::Vec3f{1.0f, 0.0f, 0.0f};
                const auto up = starforge::math::cross(right, fwd);
                matrix = starforge::math::Mat4f(1.0f);
                matrix.columns[0] = {right.x() * 0.22f, right.y() * 0.22f, right.z() * 0.22f, 0.0f};
                matrix.columns[1] = {up.x() * 0.22f, up.y() * 0.22f, up.z() * 0.22f, 0.0f};
                matrix.columns[2] = {-fwd.x() * length, -fwd.y() * length, -fwd.z() * length, 0.0f};
                matrix.columns[3] = {position.x(), position.y(), position.z(), 1.0f};
            };
            for (const auto& bolt : laserBolts)
            {
                starforge::math::Mat4f boltMatrix;
                orientBeam(boltMatrix, bolt.direction, bolt.position, 2.0f);
                starforge::Toon::Context boltContext;
                // Bolts are already in world space - applying `model` here
                // double-transformed them and pushed the beam off the reticle.
                boltContext.viewProj = viewProj * boltMatrix;
                boltContext.modelView = view * boltMatrix;
                boltContext.lightDirectionView = toonContext.lightDirectionView;
                boltContext.resolution = toonContext.resolution;
                starforge::Toon::Params boltParams;
                boltParams.capabilities = 1u;
                boltParams.baseColor = {0.03f, 0.06f, 0.12f, 1.0f};
                boltParams.emissiveColor = {0.12f, 0.6f, 1.0f, 0.55f};
                boltParams.outlineWidth = 0.0f;
                toon->record(fc, boltContext, boltParams);
            }
            // Asteroid impact / fragmentation sparks.
            for (const auto& fragment : fragments)
            {
                const float shrink = std::clamp(fragment.life / 0.45f, 0.0f, 1.0f);
                auto fm = starforge::math::Mat4f(1.0f);
                const float size = fragment.scale * shrink;
                fm.columns[0][0] = size;
                fm.columns[1][1] = size;
                fm.columns[2][2] = size;
                fm.columns[3] = {fragment.position.x(), fragment.position.y(),
                                 fragment.position.z(), 1.0f};
                starforge::Toon::Context fctx;
                fctx.viewProj = viewProj * fm;
                fctx.modelView = view * fm;
                fctx.lightDirectionView = toonContext.lightDirectionView;
                fctx.resolution = toonContext.resolution;
                starforge::Toon::Params fp;
                fp.capabilities = 1u;
                fp.baseColor = {0.12f, 0.08f, 0.04f, 1.0f};
                fp.emissiveColor = {1.0f, 0.55f, 0.18f, shrink * 0.8f};
                fp.outlineWidth = 0.0f;
                toon->record(fc, fctx, fp);
            }
            // Enemy fire: red tracers from the attacker toward the player.
            for (const auto& shot : incomingShots)
            {
                const auto toPlayer = starforge::math::Vec3f{
                    -shot.from.x(), -shot.from.y(), -shot.from.z()};
                starforge::math::Mat4f shotMatrix;
                orientBeam(shotMatrix, toPlayer, shot.from,
                           starforge::math::length(toPlayer));
                starforge::Toon::Context shotContext;
                shotContext.viewProj = viewProj * model * shotMatrix;
                shotContext.modelView = view * model * shotMatrix;
                shotContext.lightDirectionView = toonContext.lightDirectionView;
                shotContext.resolution = toonContext.resolution;
                starforge::Toon::Params shotParams;
                shotParams.capabilities = 1u;
                shotParams.baseColor = {0.10f, 0.02f, 0.01f, 1.0f};
                shotParams.emissiveColor = {0.9f, 0.18f, 0.08f,
                                            std::min(1.0f, shot.life * 4.0f)};
                shotParams.outlineWidth = 0.0f;
                toon->record(fc, shotContext, shotParams);
            }
            if (npcFighter)
                for (const auto& r : npcRenders)
                    toon->recordMesh(fc, r.context, r.params, *npcFighter);
            else
                for (const auto& r : npcRenders) toon->record(fc, r.context, r.params);
            if (flight.firePrimary) toon->record(fc, muzzleContext, muzzleParams);
            if (shieldFlash > 0.0f && shieldMesh)
                toon->recordMesh(fc, flashContext, flashParams, *shieldMesh);
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
            uiCanvas->addPanel({.x = 16.0f, .y = 16.0f, .w = 430.0f, .h = 212.0f,
                                .outlineWidth = 2.0f,
                                .fillColor = {0.05f, 0.09f, 0.18f, 0.85f},
                                .accentColor = {0.4f, 0.8f, 1.0f, 1.0f}});
            const auto hudText = [&](float tx, float ty, const char* label, float value) {
                char buffer[48];
                std::snprintf(buffer, sizeof(buffer), "%s %.0f", label, value);
                uiCanvas->addText({.x = tx, .y = ty, .scale = hudScale,
                                   .color = {0.85f, 0.95f, 1.0f, 1.0f}, .text = buffer});
            };
            hudText(28.0f, 24.0f, "THROTTLE", flight.throttle * 100.0f);
            hudText(28.0f, 48.0f, "SPEED", starforge::game::targetSpeed(flight));
            // Shield / hull readouts live inside their bars.
            const float shield = simulation.playerShield();
            const float hull = simulation.playerHull();
            uiCanvas->addBar({.x = 28.0f, .y = 80.0f, .w = 350.0f, .h = 24.0f,
                              .value = shield / 100.0f,
                              .fillColor = {0.2f, 0.6f, 1.0f, 1.0f}});
            char shieldLabel[32];
            std::snprintf(shieldLabel, sizeof(shieldLabel), "SHIELD %.0f", shield);
            uiCanvas->addText({.x = 34.0f, .y = 83.0f, .scale = hudScale,
                               .color = {0.02f, 0.05f, 0.1f, 1.0f}, .text = shieldLabel});
            // Hull bar colour tracks remaining health.
            const starforge::math::Vec4f hullColor =
                hull >= 75.0f ? starforge::math::Vec4f{0.2f, 0.9f, 0.4f, 1.0f}
              : hull >= 50.0f ? starforge::math::Vec4f{0.95f, 0.9f, 0.2f, 1.0f}
              : hull >= 25.0f ? starforge::math::Vec4f{1.0f, 0.55f, 0.15f, 1.0f}
                              : starforge::math::Vec4f{1.0f, 0.2f, 0.2f, 1.0f};
            uiCanvas->addBar({.x = 28.0f, .y = 112.0f, .w = 350.0f, .h = 24.0f,
                              .value = hull / 100.0f, .fillColor = hullColor});
            char hullLabel[32];
            std::snprintf(hullLabel, sizeof(hullLabel), "HULL %.0f", hull);
            uiCanvas->addText({.x = 34.0f, .y = 115.0f, .scale = hudScale,
                               .color = {0.02f, 0.05f, 0.1f, 1.0f}, .text = hullLabel});
            hudText(28.0f, 148.0f, "CREDITS", static_cast<float>(simulation.credits()));
            hudText(28.0f, 178.0f, "CARGO", static_cast<float>(simulation.cargoUsed()));
            uiCanvas->addText({.x = 28.0f, .y = 202.0f, .scale = 1.0f,
                               .color = {0.55f, 0.9f, 1.0f, 1.0f},
                               .text = "SYSTEM: " + simulation.currentSystem() +
                                       " - " + visual.description});
            char assetStatus[160];
            std::snprintf(assetStatus, sizeof(assetStatus),
                          "ASSETS ship %zu/station %zu parts  LOD %zu/%zu  MIP %zu",
                          shipModel.parts.size(), stationModel.parts.size(),
                          shipModel.activeLod, stationModel.activeLod,
                          shipModel.parts.empty() ? 0 : shipModel.parts.front().activeMip);
            uiCanvas->addText({.x = 28.0f, .y = 214.0f, .scale = 1.0f,
                               .color = {0.6f, 0.85f, 0.7f, 1.0f}, .text = assetStatus});

            // Crosshair: four strokes + centre dot, all around screen centre.
            const auto reticle = starforge::math::Vec4f{1.0f, 1.0f, 1.0f, 0.95f};
            uiCanvas->addRect({.x = cx - 20.0f, .y = cy - 1.0f, .w = 12.0f, .h = 2.0f, .color = reticle});
            uiCanvas->addRect({.x = cx + 8.0f, .y = cy - 1.0f, .w = 12.0f, .h = 2.0f, .color = reticle});
            uiCanvas->addRect({.x = cx - 1.0f, .y = cy - 20.0f, .w = 2.0f, .h = 12.0f, .color = reticle});
            uiCanvas->addRect({.x = cx - 1.0f, .y = cy + 8.0f, .w = 2.0f, .h = 12.0f, .color = reticle});
            uiCanvas->addRect({.x = cx - 2.0f, .y = cy - 2.0f, .w = 4.0f, .h = 4.0f, .color = reticle});
            // Target box follows the selected NPC on screen (or the nearest
            // hostile when nothing is locked), with a direction chevron.
            const auto* selectedContact = targeting.selected();
            const starforge::game::NpcInfo* trackedNpc = nullptr;
            if (selectedContact)
            {
                for (const auto& npc : simulation.npcs())
                    if (npc.alive && npc.id == selectedContact->id)
                    {
                        trackedNpc = &npc;
                        break;
                    }
            }
            if (!trackedNpc)
            {
                float best = 1e30f;
                for (const auto& npc : simulation.npcs())
                {
                    if (!npc.alive || !npc.hostile) continue;
                    const float d = std::sqrt(npc.position.x * npc.position.x +
                                              npc.position.y * npc.position.y +
                                              npc.position.z * npc.position.z);
                    if (d < best) { best = d; trackedNpc = &npc; }
                }
            }
            if (trackedNpc)
            {
                const bool lockedTarget = selectedContact &&
                                          selectedContact->id == trackedNpc->id &&
                                          trackedNpc->hostile;
                const starforge::math::Vec4f hostileTc{1.0f, 0.3f, 0.2f, 1.0f};
                const starforge::math::Vec4f neutralTc{0.3f, 1.0f, 0.85f, 1.0f};
                const starforge::math::Vec4f tc = trackedNpc->hostile ? hostileTc : neutralTc;
                const starforge::math::Vec4f boxColor = lockedTarget || selectedContact
                    ? tc
                    : starforge::math::Vec4f{tc.x(), tc.y(), tc.z(), 0.5f};

                const starforge::math::Vec4f clip =
                    // NPC positions are camera-relative WORLD offsets (ship at
                    // origin, world axes) - the same frame the NPC mesh and
                    // radar use. Applying `model` here double-rotates the box
                    // away from the ship.
                    viewProj * starforge::math::Vec4f{trackedNpc->position.x,
                                                      trackedNpc->position.y,
                                                      trackedNpc->position.z, 1.0f};
                const float tw = static_cast<float>(size.width);
                const float th = static_cast<float>(size.height);
                bool onScreen = false;
                float tx = cx, ty = cy;
                if (clip.w() > 1e-3f)
                {
                    tx = (clip.x() / clip.w() * 0.5f + 0.5f) * tw;
                    ty = (0.5f - clip.y() / clip.w() * 0.5f) * th;
                    onScreen = tx > 20.0f && tx < tw - 20.0f && ty > 20.0f && ty < th - 20.0f;
                }
                if (onScreen)
                {
                    const float bx = tx - 26.0f, by = ty - 26.0f;
                    uiCanvas->addRect({.x = bx, .y = by, .w = 12.0f, .h = 2.0f, .color = boxColor});
                    uiCanvas->addRect({.x = bx + 40.0f, .y = by, .w = 12.0f, .h = 2.0f, .color = boxColor});
                    uiCanvas->addRect({.x = bx, .y = by + 50.0f, .w = 12.0f, .h = 2.0f, .color = boxColor});
                    uiCanvas->addRect({.x = bx + 40.0f, .y = by + 50.0f, .w = 12.0f, .h = 2.0f, .color = boxColor});
                    uiCanvas->addRect({.x = bx, .y = by, .w = 2.0f, .h = 12.0f, .color = boxColor});
                    uiCanvas->addRect({.x = bx + 50.0f, .y = by, .w = 2.0f, .h = 12.0f, .color = boxColor});
                    uiCanvas->addRect({.x = bx, .y = by + 40.0f, .w = 2.0f, .h = 12.0f, .color = boxColor});
                    uiCanvas->addRect({.x = bx + 50.0f, .y = by + 40.0f, .w = 2.0f, .h = 12.0f, .color = boxColor});
                }
                const float aimX = tx - cx, aimY = ty - cy;
                const float aimLen = std::sqrt(aimX * aimX + aimY * aimY);
                if (!onScreen || aimLen > 120.0f)
                {
                    // Prominent arrow near the screen edge pointing at the
                    // target, so a locked ship behind you can still be found.
                    const float ux = aimLen > 1e-3f ? aimX / aimLen : 0.0f;
                    const float uy = aimLen > 1e-3f ? aimY / aimLen : -1.0f;
                    const float edge = std::min(tw, th) * 0.36f;
                    const float px = cx + ux * edge, py = cy + uy * edge;
                    const starforge::math::Vec4f arrow{1.0f, 0.55f, 0.15f, 1.0f};
                    uiCanvas->addRect({.x = px - 12.0f, .y = py - 4.0f, .w = 24.0f, .h = 8.0f, .color = arrow});
                    uiCanvas->addRect({.x = px - 8.0f, .y = py - 8.0f, .w = 16.0f, .h = 16.0f, .color = arrow});
                    char offText[64];
                    const float range = std::sqrt(
                        trackedNpc->position.x * trackedNpc->position.x +
                        trackedNpc->position.y * trackedNpc->position.y +
                        trackedNpc->position.z * trackedNpc->position.z);
                    std::snprintf(offText, sizeof(offText), "TARGET %.0fm - TURN", range);
                    uiCanvas->addText({.x = px - 44.0f, .y = py + 14.0f, .scale = 1.0f,
                                       .color = arrow, .text = offText});
                }
                if (lockedTarget && targeting.lockProgress() > 0.0f)
                    uiCanvas->addBar({.x = cx - 40.0f, .y = cy + 44.0f, .w = 80.0f, .h = 6.0f,
                                      .value = targeting.lockProgress(), .fillColor = tc});
            }

            // Contact markers: every live NPC gets a small diamond + range so
            // fast-moving ships are always findable, not just the locked one.
            const float markerW = static_cast<float>(size.width);
            const float markerH = static_cast<float>(size.height);
            for (const auto& npc : simulation.npcs())
            {
                if (!npc.alive) continue;
                const auto clip = viewProj * starforge::math::Vec4f{
                    npc.position.x, npc.position.y, npc.position.z, 1.0f};
                if (clip.w() <= 1e-3f) continue;
                const float mx = (clip.x() / clip.w() * 0.5f + 0.5f) * markerW;
                const float my = (0.5f - clip.y() / clip.w() * 0.5f) * markerH;
                if (mx < 8.0f || mx > markerW - 8.0f || my < 8.0f || my > markerH - 8.0f) continue;
                const bool selected = targeting.selected() &&
                                      targeting.selected()->id == npc.id;
                if (selected) continue; // the big target box already covers it
                const auto mc = npc.hostile
                    ? starforge::math::Vec4f{1.0f, 0.35f, 0.2f, 0.9f}
                    : starforge::math::Vec4f{0.3f, 1.0f, 0.85f, 0.9f};
                uiCanvas->addRect({.x = mx - 3.0f, .y = my - 1.0f, .w = 6.0f, .h = 2.0f, .color = mc});
                uiCanvas->addRect({.x = mx - 1.0f, .y = my - 3.0f, .w = 2.0f, .h = 6.0f, .color = mc});
                char contactLabel[32];
                const float range = std::sqrt(npc.position.x * npc.position.x +
                                              npc.position.y * npc.position.y +
                                              npc.position.z * npc.position.z);
                std::snprintf(contactLabel, sizeof(contactLabel), "%.0fm", range);
                uiCanvas->addText({.x = mx + 6.0f, .y = my - 4.0f, .scale = 1.0f,
                                   .color = mc, .text = contactLabel});
            }
            // Proximity station prompt: appears within 1 km, above Comms.
            if (!flight.paused && !chatActive && !stationOpen &&
                stationRangeMetres < 1000.0)
            {
                const auto prompt = starforge::game::stationPromptRect(
                    static_cast<float>(size.width), static_cast<float>(size.height));
                uiCanvas->addPanel({.x = prompt.panel.x, .y = prompt.panel.y,
                                    .w = prompt.panel.w, .h = prompt.panel.h,
                                    .outlineWidth = 1.5f,
                                    .fillColor = {0.06f, 0.12f, 0.16f, 0.92f},
                                    .accentColor = {0.95f, 0.75f, 0.25f, 1.0f}});
                char promptText[96];
                std::snprintf(promptText, sizeof(promptText),
                              "STATION A  %.0f m  -  SELECT ACTION", stationRangeMetres);
                uiCanvas->addText({.x = prompt.panel.x + 10.0f, .y = prompt.panel.y + 6.0f,
                                   .scale = 1.0f, .color = {1.0f, 0.9f, 0.5f, 1.0f},
                                   .text = promptText});
                for (const auto& [id, label, hot] : {
                         std::tuple<const char*, const char*, bool>{
                             "prompt:dock", "<DOCK>", uiState.hovered("prompt:dock")},
                         std::tuple<const char*, const char*, bool>{
                             "prompt:chat", "<CHAT>", uiState.hovered("prompt:chat")}})
                {
                    const auto& rect = std::string(id) == "prompt:dock" ? prompt.dock : prompt.chat;
                    uiCanvas->addPanel({.x = rect.x, .y = rect.y, .w = rect.w, .h = rect.h,
                                        .outlineWidth = 1.0f,
                                        .fillColor = hot
                                            ? starforge::math::Vec4f{0.25f, 0.45f, 0.3f, 1.0f}
                                            : starforge::math::Vec4f{0.10f, 0.2f, 0.28f, 1.0f}});
                    uiCanvas->addText({.x = rect.x + 42.0f, .y = rect.y + 7.0f, .scale = 2.0f,
                                       .color = {0.95f, 0.98f, 1.0f, 1.0f}, .text = label});
                }
            }

            // M16.7: comms panel is ALWAYS visible bottom-left with a full
            // scrollable history and an always-present chat input box. Combat
            // alerts go to toasts only (top-centre), so they are not doubled.
            {
                const float commsX = 16.0f;
                const float commsY = static_cast<float>(size.height) - 232.0f;
                constexpr std::size_t kCommsLines = 7;
                uiCanvas->addPanel({.x = commsX, .y = commsY, .w = 420.0f, .h = 216.0f,
                                    .outlineWidth = 2.0f,
                                    .fillColor = {0.04f, 0.08f, 0.14f, 0.9f},
                                    .accentColor = {0.35f, 0.8f, 1.0f, 1.0f}});
                uiCanvas->addText({.x = commsX + 10.0f, .y = commsY + 14.0f, .scale = 2.0f,
                                   .color = {0.4f, 0.9f, 1.0f, 1.0f},
                                   .text = chatActive ? "COMMS  [CHAT]" : "COMMS"});
                if (comms.scrolledUp())
                {
                    uiCanvas->addText({.x = commsX + 250.0f, .y = commsY + 14.0f, .scale = 1.0f,
                                       .color = {1.0f, 0.85f, 0.4f, 1.0f},
                                       .text = "PgUp/PgDn - history"});
                }
                const auto& messages = comms.messages();
                const auto [first, last] = comms.visibleRange(kCommsLines);
                std::size_t row = 0;
                for (std::size_t i = first; i < last; ++i)
                {
                    const auto& msg = messages[i];
                    if (msg.channel == starforge::game::CommsChannel::Combat) continue;
                    std::string text = msg.sender + ": " + msg.text;
                    // Panel is 420 px wide with a 10 px inset; at scale 2 a
                    // glyph cell is 12 px, so 32 chars is the safe maximum.
                    if (text.size() > 32) text.resize(32);
                    const float ly = commsY + 32.0f + static_cast<float>(row++) * 19.0f;
                    const auto lc = msg.channel == starforge::game::CommsChannel::Trade
                        ? starforge::math::Vec4f{0.3f, 1.0f, 0.6f, 1.0f}
                        : msg.channel == starforge::game::CommsChannel::Local
                            ? starforge::math::Vec4f{1.0f, 0.95f, 0.7f, 1.0f}
                            : starforge::math::Vec4f{0.75f, 0.9f, 1.0f, 1.0f};
                    uiCanvas->addText({.x = commsX + 10.0f, .y = ly, .scale = 2.0f,
                                       .color = lc, .text = text});
                }
                // Always-visible chat input box. Enter toggles it; type to
                // talk to sector traffic (canned replies until chat AI lands).
                const float inputY = commsY + 178.0f;
                uiCanvas->addRect({.x = commsX + 8.0f, .y = inputY, .w = 404.0f, .h = 28.0f,
                                   .color = chatActive
                                       ? starforge::math::Vec4f{0.10f, 0.28f, 0.34f, 1.0f}
                                       : starforge::math::Vec4f{0.07f, 0.13f, 0.20f, 1.0f}});
                std::string entry = chatActive
                    ? (textEntry.prompt() + textEntry.text())
                    : std::string("> press ENTER to chat");
                if (chatActive && static_cast<int>(time * 2.0f) % 2 == 0) entry += "_";
                if (entry.size() > 32) entry = entry.substr(entry.size() - 32);
                uiCanvas->addText({.x = commsX + 14.0f, .y = inputY + 6.0f, .scale = 2.0f,
                                   .color = chatActive
                                       ? starforge::math::Vec4f{0.6f, 1.0f, 0.8f, 1.0f}
                                       : starforge::math::Vec4f{0.5f, 0.65f, 0.75f, 1.0f},
                                   .text = entry});
            }

            // M16.6: navigation radar (bottom-right) + destination readout.
            {
                const float kRadarRange = radarRange;
                constexpr float kRadarRadius = 110.0f;
                const float rx = static_cast<float>(size.width) - 256.0f;
                const float ry = static_cast<float>(size.height) - 256.0f;
                const float ccx = rx + 120.0f;
                const float ccy = ry + 128.0f;
                uiCanvas->addPanel({.x = rx, .y = ry, .w = 240.0f, .h = 240.0f,
                                    .outlineWidth = 2.0f,
                                    .fillColor = {0.04f, 0.08f, 0.14f, 0.85f}});
                const starforge::math::Vec4f ringColor{0.15f, 0.35f, 0.5f, 1.0f};
                for (float r : {kRadarRadius * 0.5f, kRadarRadius})
                {
                    uiCanvas->addRect({.x = ccx - r, .y = ccy - 1.0f, .w = 2.0f * r, .h = 1.0f, .color = ringColor});
                    uiCanvas->addRect({.x = ccx - r, .y = ccy + r - 1.0f, .w = 2.0f * r, .h = 1.0f, .color = ringColor});
                    uiCanvas->addRect({.x = ccx - 1.0f, .y = ccy - r, .w = 1.0f, .h = 2.0f * r, .color = ringColor});
                    uiCanvas->addRect({.x = ccx + r - 1.0f, .y = ccy - r, .w = 1.0f, .h = 2.0f * r, .color = ringColor});
                }
                const auto radarColor = [](starforge::game::RadarKind kind) -> starforge::math::Vec4f {
                    switch (kind)
                    {
                    case starforge::game::RadarKind::Hostile: return {1.0f, 0.25f, 0.2f, 1.0f};
                    case starforge::game::RadarKind::Neutral: return {0.3f, 1.0f, 0.85f, 1.0f};
                    case starforge::game::RadarKind::Planet:  return {0.2f, 0.55f, 1.0f, 1.0f};
                    case starforge::game::RadarKind::Station: return {0.3f, 1.0f, 0.6f, 1.0f};
                    default:                                  return {0.7f, 0.7f, 0.7f, 1.0f};
                    }
                };
                // M16 polish: continuous rotating sweep + an expanding ping
                // every few seconds, so the radar reads as an active scanner.
                {
                    const float sweep = time * 1.7f;
                    const float sweepUx = std::cos(sweep), sweepUy = std::sin(sweep);
                    const starforge::math::Vec4f sweepColor{0.2f, 0.9f, 0.7f, 0.55f};
                    for (int i = 1; i <= 22; ++i)
                    {
                        const float t = static_cast<float>(i) / 22.0f * kRadarRadius;
                        uiCanvas->addRect({.x = ccx + sweepUx * t - 1.0f,
                                           .y = ccy + sweepUy * t - 1.0f,
                                           .w = 2.0f, .h = 2.0f, .color = sweepColor});
                    }
                    const float ping = std::fmod(time, 3.0f) / 3.0f;
                    const float pingR = 12.0f + ping * (kRadarRadius - 12.0f);
                    const starforge::math::Vec4f pingColor{0.3f, 1.0f, 0.8f,
                                                            (1.0f - ping) * 0.45f};
                    for (int i = 0; i < 24; ++i)
                    {
                        const float a = static_cast<float>(i) / 24.0f * 6.2831853f;
                        uiCanvas->addRect({.x = ccx + std::cos(a) * pingR - 1.0f,
                                           .y = ccy + std::sin(a) * pingR - 1.0f,
                                           .w = 3.0f, .h = 3.0f, .color = pingColor});
                    }
                }
                // Contacts arrive as camera-relative WORLD offsets. Rotate
                // them into the ship's local basis so a 180 turn moves them
                // from behind to ahead on the radar.
                const auto shipBasis = matFromQuat(shipQuat);
                const starforge::math::Vec3f shipRight{shipBasis.columns[0][0],
                                                       shipBasis.columns[0][1],
                                                       shipBasis.columns[0][2]};
                const auto shipForwardWorld = forwardFromQuat(shipQuat);
                const starforge::math::Vec3f shipForward{
                    static_cast<float>(shipForwardWorld.x()),
                    static_cast<float>(shipForwardWorld.y()),
                    static_cast<float>(shipForwardWorld.z())};
                const auto plotWorld = [&](const char* id, float wx, float wy, float wz,
                                           starforge::game::RadarKind kind) {
                    const auto [localX, localF] = starforge::game::toShipLocal(
                        {wx, wy, wz}, {shipRight.x(), shipRight.y(), shipRight.z()},
                        {shipForward.x(), shipForward.y(), shipForward.z()});
                    const auto p = starforge::game::projectRadar(
                        {id, localX, localF, kind}, kRadarRange, kRadarRadius);
                    if (!p.visible) return;
                    const bool threat = kind == starforge::game::RadarKind::Hostile;
                    const float dotSize = threat ? 5.0f : 3.0f;
                    uiCanvas->addRect({.x = ccx + p.x - dotSize * 0.5f,
                                       .y = ccy + p.y - dotSize * 0.5f,
                                       .w = dotSize, .h = dotSize, .color = radarColor(kind)});
                };
                plotWorld("planet", planetRel.x(), planetRel.y(), planetRel.z(),
                          starforge::game::RadarKind::Planet);
                plotWorld("station-a", 400.0f, 0.0f, -1200.0f, starforge::game::RadarKind::Station);
                for (const auto& npc : simulation.npcs())
                    if (npc.alive)
                        plotWorld(npc.id.c_str(), npc.position.x, npc.position.y,
                                  npc.position.z,
                                  npc.hostile ? starforge::game::RadarKind::Hostile
                                              : starforge::game::RadarKind::Neutral);
                int radarRocks = 0;
                for (const auto* rock : sorted)
                {
                    if (radarRocks++ >= 12) break; // keep the radar readable
                    plotWorld("rock", static_cast<float>(rock->position.x),
                              static_cast<float>(rock->position.y),
                              static_cast<float>(rock->position.z),
                              starforge::game::RadarKind::Rock);
                }
                // Selected target wears a yellow bracket on the radar, so it
                // is obvious which blip is locked (no target = no bracket).
                if (const auto* target = targeting.selected())
                {
                    float wx = 400.0f, wy = 0.0f, wz = -1200.0f;
                    for (const auto& npc : simulation.npcs())
                        if (npc.alive && npc.id == target->id)
                        {
                            wx = npc.position.x; wy = npc.position.y; wz = npc.position.z;
                            break;
                        }
                    const auto [lx, lf] = starforge::game::toShipLocal(
                        {wx, wy, wz}, {shipRight.x(), shipRight.y(), shipRight.z()},
                        {shipForward.x(), shipForward.y(), shipForward.z()});
                    const auto tp = starforge::game::projectRadar(
                        {target->id, lx, lf, starforge::game::RadarKind::Station},
                        kRadarRange, kRadarRadius);
                    if (tp.visible)
                    {
                        const starforge::math::Vec4f marker{1.0f, 0.92f, 0.25f, 1.0f};
                        const float bx = ccx + tp.x, by = ccy + tp.y;
                        uiCanvas->addRect({.x = bx - 8.0f, .y = by - 8.0f, .w = 6.0f, .h = 2.0f, .color = marker});
                        uiCanvas->addRect({.x = bx + 2.0f, .y = by - 8.0f, .w = 6.0f, .h = 2.0f, .color = marker});
                        uiCanvas->addRect({.x = bx - 8.0f, .y = by + 6.0f, .w = 6.0f, .h = 2.0f, .color = marker});
                        uiCanvas->addRect({.x = bx + 2.0f, .y = by + 6.0f, .w = 6.0f, .h = 2.0f, .color = marker});
                    }
                }
                uiCanvas->addRect({.x = ccx - 2.0f, .y = ccy - 2.0f, .w = 4.0f, .h = 4.0f,
                                   .color = {1.0f, 1.0f, 1.0f, 1.0f}}); // player
                char radarLabel[32];
                std::snprintf(radarLabel, sizeof(radarLabel), "RADAR %.1fkm (+/-)",
                              radarRange / 1000.0f);
                uiCanvas->addText({.x = rx + 8.0f, .y = ry + 12.0f, .scale = 2.0f,
                                   .color = {0.6f, 0.9f, 1.0f, 1.0f}, .text = radarLabel});
                // Selected-target navigation readout, placed directly ABOVE
                // the radar. BRG is degrees clockwise from straight ahead.
                if (const auto* target = targeting.selected())
                {
                    float wx = 400.0f, wy = 0.0f, wz = -1200.0f; // station fallback
                    for (const auto& npc : simulation.npcs())
                        if (npc.alive && npc.id == target->id)
                        {
                            wx = npc.position.x;
                            wy = npc.position.y;
                            wz = npc.position.z;
                            break;
                        }
                    const auto [localX, localF] = starforge::game::toShipLocal(
                        {wx, wy, wz}, {shipRight.x(), shipRight.y(), shipRight.z()},
                        {shipForward.x(), shipForward.y(), shipForward.z()});
                    const auto p = starforge::game::projectRadar(
                        {target->id, localX, localF, starforge::game::RadarKind::Station},
                        kRadarRange, kRadarRadius);
                    char nav[96];
                    std::snprintf(nav, sizeof(nav), "NAV TGT %s  %.0fm  BRG %.0fdeg",
                                  target->id.c_str(), p.distance, p.bearing);
                    uiCanvas->addText({.x = rx + 4.0f, .y = ry - 26.0f,
                                       .scale = 2.0f,
                                       .color = {0.8f, 0.95f, 1.0f, 1.0f}, .text = nav});
                }
                else
                {
                    uiCanvas->addText({.x = rx + 4.0f, .y = ry - 26.0f, .scale = 2.0f,
                                       .color = {0.45f, 0.6f, 0.7f, 1.0f},
                                       .text = "NO TARGET - press T to lock"});
                }
            }
            // Station trade / mission interface (opened via dock prompt only).
            if (stationOpen && flight.mode == starforge::game::FlightMode::Ui)
            {
                const auto panel = starforge::game::stationPanelRect(
                    static_cast<float>(size.width), static_cast<float>(size.height));
                uiCanvas->addPanel({.x = panel.x, .y = panel.y, .w = panel.w, .h = panel.h,
                                    .outlineWidth = 2.0f,
                                    .fillColor = {0.06f, 0.11f, 0.2f, 0.92f},
                                    .accentColor = {0.95f, 0.7f, 0.2f, 1.0f}});
                uiCanvas->addText({.x = panel.x + 14.0f, .y = panel.y + 16.0f, .scale = 2.0f,
                                   .color = {0.9f, 0.95f, 1.0f, 1.0f}, .text = "STATION A"});
                // Two tabs: Trading and Missions.
                for (const auto& tab : {
                         std::tuple<StationTab, float, const char*, bool>{
                             StationTab::Trade, panel.x + 18.0f, "TRADING",
                             stationTab == StationTab::Trade},
                         std::tuple<StationTab, float, const char*, bool>{
                             StationTab::Missions, panel.x + 176.0f, "MISSIONS",
                             stationTab == StationTab::Missions}})
                {
                    const auto tabX = std::get<1>(tab);
                    const bool active = std::get<3>(tab);
                    uiCanvas->addPanel({.x = tabX, .y = panel.y + 50.0f,
                                        .w = 150.0f, .h = 28.0f, .outlineWidth = 1.0f,
                                        .fillColor = active
                                            ? starforge::math::Vec4f{0.18f, 0.38f, 0.42f, 1.0f}
                                            : starforge::math::Vec4f{0.08f, 0.15f, 0.24f, 1.0f},
                                        .accentColor = active
                                            ? starforge::math::Vec4f{0.4f, 1.0f, 0.8f, 1.0f}
                                            : starforge::math::Vec4f{0.3f, 0.5f, 0.7f, 1.0f}});
                    uiCanvas->addText({.x = tabX + 14.0f, .y = panel.y + 57.0f, .scale = 2.0f,
                                       .color = active
                                           ? starforge::math::Vec4f{0.9f, 1.0f, 0.95f, 1.0f}
                                           : starforge::math::Vec4f{0.5f, 0.7f, 0.85f, 1.0f},
                                       .text = std::get<2>(tab)});
                }
                char status[96];
                std::snprintf(status, sizeof(status), "CR %.0f  ORE %d  %s",
                              static_cast<float>(simulation.credits()),
                              simulation.cargoCount("ore"),
                              simulation.isDocked() ? "DOCKED" : "APPROACH");
                uiCanvas->addText({.x = panel.x + 14.0f, .y = panel.y + 40.0f, .scale = 1.0f,
                                   .color = {0.6f, 0.9f, 0.7f, 1.0f}, .text = status});
                // Quest status so the haul mission is discoverable/completable.
                std::string quest;
                if (simulation.missionCompleted("haul-ore"))
                    quest = "HAUL: complete (+500cr, range 2.0ly)";
                else if (simulation.missionActive("haul-ore"))
                    quest = "HAUL: deliver 10 ore to station-b, then COMPLETE";
                else
                    quest = "HAUL: accept contract to begin";
                uiCanvas->addText({.x = panel.x + 14.0f, .y = panel.y + 364.0f, .scale = 1.0f,
                                   .color = {1.0f, 0.85f, 0.45f, 1.0f}, .text = quest});
                for (const auto& button : starforge::game::stationButtons())
                {
                    // "dock" is intentionally omitted: the Trading view is
                    // only reachable after docking, so a DOCK button is noise.
                    const bool onTrade = button.id == "buy-5" || button.id == "sell-5" ||
                                         button.id == "undock";
                    const bool always = button.id == "close";
                    if (!always && (stationTab == StationTab::Trade ? !onTrade : onTrade))
                        continue;
                    const bool hot = uiState.hovered(button.id);
                    uiCanvas->addPanel({.x = panel.x + button.x, .y = panel.y + button.y,
                                        .w = button.w, .h = button.h, .outlineWidth = 1.5f,
                                        .fillColor = hot ? starforge::math::Vec4f{0.2f, 0.45f, 0.3f, 1.0f}
                                                         : starforge::math::Vec4f{0.1f, 0.2f, 0.32f, 1.0f},
                                        .outlineColor = {0.02f, 0.04f, 0.08f, 1.0f},
                                        .accentColor = {0.95f, 0.7f, 0.2f, 1.0f}});
                    uiCanvas->addText({.x = panel.x + button.x + 8.0f,
                                       .y = panel.y + button.y + 10.0f, .scale = 2.0f,
                                       .color = {0.95f, 0.98f, 1.0f, 1.0f}, .text = button.label});
                }
            }

            // M16.9: toast notifications (top-centre, transient).
            {
                const auto& active = toasts.messages();
                const float tx = static_cast<float>(size.width) * 0.5f - 160.0f;
                for (std::size_t i = 0; i < active.size() && i < 3; ++i)
                {
                    uiCanvas->addPanel({.x = tx, .y = 20.0f + static_cast<float>(i) * 32.0f,
                                        .w = 320.0f, .h = 28.0f, .outlineWidth = 1.5f,
                                        .fillColor = {0.25f, 0.1f, 0.06f, 0.9f},
                                        .accentColor = {1.0f, 0.6f, 0.15f, 1.0f}});
                    uiCanvas->addText({.x = tx + 10.0f, .y = 25.0f + static_cast<float>(i) * 32.0f,
                                       .scale = 2.0f,
                                       .color = {1.0f, 0.85f, 0.4f, 1.0f},
                                       .text = active[i].text});
                }
            }

            // M16.9: sci-fi sector chart with a separate info column.
            if (mapVisible)
            {
                const auto layout = starforge::game::mapLayout(
                    static_cast<float>(size.width), static_cast<float>(size.height),
                    simulation.currentSystem(), simulation.jumpRangeLy());
                const auto& ms = layout.mapSection;
                const auto& is = layout.infoSection;
                uiCanvas->addPanel({.x = layout.panel.x, .y = layout.panel.y,
                                    .w = layout.panel.w, .h = layout.panel.h,
                                    .outlineWidth = 2.0f,
                                    .fillColor = {0.02f, 0.04f, 0.09f, 0.96f},
                                    .accentColor = {0.3f, 0.8f, 1.0f, 1.0f}});

                // Header strip + section captions.
                uiCanvas->addRect({.x = layout.panel.x + 2.0f, .y = layout.panel.y + 34.0f,
                                   .w = layout.panel.w - 4.0f, .h = 2.0f,
                                   .color = {0.15f, 0.4f, 0.55f, 1.0f}});
                uiCanvas->addText({.x = layout.panel.x + 14.0f, .y = layout.panel.y + 14.0f,
                                   .scale = 2.0f, .color = {0.5f, 0.9f, 1.0f, 1.0f},
                                   .text = "SECTOR CHART"});
                uiCanvas->addText({.x = is.x + 4.0f, .y = ms.y + 4.0f, .scale = 1.0f,
                                   .color = {0.5f, 0.85f, 1.0f, 1.0f},
                                   .text = "DESTINATION DATA"});

                // Chart backdrop: deterministic star dots + nav grid.
                const starforge::math::Vec4f starColor{0.45f, 0.7f, 0.85f, 0.5f};
                for (int i = 0; i < 90; ++i)
                {
                    const float rx = std::fmod(static_cast<float>(i) * 0.6180339f, 1.0f);
                    const float ry = std::fmod(static_cast<float>(i) * 0.4142135f, 1.0f);
                    uiCanvas->addRect({.x = ms.x + rx * ms.w, .y = ms.y + ry * ms.h,
                                       .w = (i % 7 == 0) ? 2.0f : 1.0f,
                                       .h = (i % 7 == 0) ? 2.0f : 1.0f, .color = starColor});
                }
                const starforge::math::Vec4f gridColor{0.12f, 0.3f, 0.42f, 0.55f};
                for (float gx = ms.x; gx <= ms.x + ms.w; gx += 56.0f)
                    uiCanvas->addRect({.x = gx, .y = ms.y, .w = 1.0f, .h = ms.h, .color = gridColor});
                for (float gy = ms.y; gy <= ms.y + ms.h; gy += 56.0f)
                    uiCanvas->addRect({.x = ms.x, .y = gy, .w = ms.w, .h = 1.0f, .color = gridColor});
                // Dashed range rings around the current position.
                for (float r : {70.0f, 140.0f, 210.0f})
                {
                    for (int i = 0; i < 48; ++i)
                    {
                        if (i % 2) continue;
                        const float a = static_cast<float>(i) / 48.0f * 6.2831853f;
                        uiCanvas->addRect({.x = layout.centreX + std::cos(a) * r - 1.0f,
                                           .y = layout.centreY + std::sin(a) * r - 1.0f,
                                           .w = 2.0f, .h = 2.0f,
                                           .color = {0.18f, 0.45f, 0.6f, 0.6f}});
                    }
                }
                // Bright cyan corner brackets frame the chart.
                const starforge::math::Vec4f bracket{0.3f, 0.9f, 1.0f, 1.0f};
                for (const auto& c : {std::pair<float, float>{ms.x, ms.y},
                                      std::pair<float, float>{ms.x + ms.w, ms.y},
                                      std::pair<float, float>{ms.x, ms.y + ms.h},
                                      std::pair<float, float>{ms.x + ms.w, ms.y + ms.h}})
                {
                    const bool left = c.first < layout.centreX;
                    const bool top = c.second < layout.centreY;
                    uiCanvas->addRect({.x = left ? c.first : c.first - 22.0f,
                                       .y = c.second, .w = 22.0f, .h = 2.0f, .color = bracket});
                    uiCanvas->addRect({.x = c.first,
                                       .y = top ? c.second : c.second - 22.0f,
                                       .w = 2.0f, .h = 22.0f, .color = bracket});
                }

                // Glowing divider between chart and info column.
                uiCanvas->addRect({.x = layout.dividerX, .y = ms.y, .w = 2.0f, .h = ms.h,
                                   .color = {0.3f, 0.8f, 1.0f, 0.9f}});

                const starforge::math::Vec4f currentColor{0.25f, 1.0f, 0.5f, 1.0f};
                const starforge::math::Vec4f selectedColor{1.0f, 0.9f, 0.2f, 1.0f};
                const starforge::game::MapPoint* currentPoint = nullptr;
                const starforge::game::MapPoint* selectedPoint = nullptr;
                for (const auto& sys : layout.systems)
                {
                    if (sys.current) currentPoint = &sys.point;
                    if (sys.id == selectedSystem) selectedPoint = &sys.point;
                }

                // Slim dotted route between current and selected destination.
                if (currentPoint && selectedPoint && currentPoint != selectedPoint)
                {
                    for (int step = 0; step <= 60; ++step)
                    {
                        if (step % 3) continue;
                        const float t = static_cast<float>(step) / 60.0f;
                        const float px = layout.centreX + currentPoint->x +
                                         (selectedPoint->x - currentPoint->x) * t;
                        const float py = layout.centreY + currentPoint->y +
                                         (selectedPoint->y - currentPoint->y) * t;
                        uiCanvas->addRect({.x = px - 1.0f, .y = py - 1.0f, .w = 2.0f, .h = 2.0f,
                                           .color = {1.0f, 0.9f, 0.3f, 0.85f}});
                    }
                }

                // Systems: current = greenish, selection = yellow, others cool.
                for (const auto& sys : layout.systems)
                {
                    const auto dotColor = sys.current ? currentColor
                        : sys.id == selectedSystem ? selectedColor
                        : sys.point.reachable
                            ? starforge::math::Vec4f{0.3f, 0.8f, 1.0f, 1.0f}
                            : starforge::math::Vec4f{0.45f, 0.5f, 0.65f, 1.0f};
                    if (sys.current)
                    {
                        const float pulse = 10.0f + 2.0f * std::sin(time * 3.0f);
                        uiCanvas->addRect({.x = layout.centreX + sys.point.x - pulse,
                                           .y = layout.centreY + sys.point.y - 1.0f,
                                           .w = pulse * 2.0f, .h = 2.0f,
                                           .color = {currentColor.x(), currentColor.y(),
                                                     currentColor.z(), 0.5f}});
                        uiCanvas->addRect({.x = layout.centreX + sys.point.x - 1.0f,
                                           .y = layout.centreY + sys.point.y - pulse,
                                           .w = 2.0f, .h = pulse * 2.0f,
                                           .color = {currentColor.x(), currentColor.y(),
                                                     currentColor.z(), 0.5f}});
                    }
                    const float dot = sys.current || sys.id == selectedSystem ? 9.0f : 6.0f;
                    uiCanvas->addRect({.x = layout.centreX + sys.point.x - dot * 0.5f,
                                       .y = layout.centreY + sys.point.y - dot * 0.5f,
                                       .w = dot, .h = dot, .color = dotColor});
                    float lx = layout.centreX + sys.point.x + 10.0f;
                    float ly = layout.centreY + sys.point.y + 8.0f;
                    lx = std::clamp(lx, ms.x + 4.0f, ms.x + ms.w - 96.0f);
                    ly = std::clamp(ly, ms.y + 4.0f, ms.y + ms.h - 12.0f);
                    uiCanvas->addText({.x = lx, .y = ly, .scale = 1.0f,
                                       .color = dotColor, .text = sys.id});
                }

                // --- Information column (never overlaps the chart) ---
                const float ix = is.x + 8.0f;
                std::string dest = selectedSystem;
                if (dest.size() > 19) dest.resize(19);
                uiCanvas->addText({.x = ix, .y = is.y + 24.0f, .scale = 1.0f,
                                   .color = {0.55f, 0.75f, 0.9f, 1.0f}, .text = "SYSTEM"});
                uiCanvas->addText({.x = ix, .y = is.y + 38.0f, .scale = 2.0f,
                                   .color = selectedColor, .text = dest});
                const bool reachable = selectedPoint && selectedPoint->reachable;
                uiCanvas->addText({.x = ix, .y = is.y + 66.0f, .scale = 1.0f,
                                   .color = reachable
                                       ? starforge::math::Vec4f{0.3f, 1.0f, 0.6f, 1.0f}
                                       : starforge::math::Vec4f{1.0f, 0.5f, 0.3f, 1.0f},
                                   .text = reachable ? "IN JUMP RANGE" : "OUT OF RANGE"});
                if (selectedPoint)
                {
                    char dtext[48];
                    std::snprintf(dtext, sizeof(dtext), "DIST %.2f ly", selectedPoint->distance);
                    uiCanvas->addText({.x = ix, .y = is.y + 84.0f, .scale = 1.0f,
                                       .color = {0.7f, 0.85f, 0.95f, 1.0f}, .text = dtext});
                }
                uiCanvas->addText({.x = ix, .y = is.y + 116.0f, .scale = 1.0f,
                                   .color = {0.55f, 0.75f, 0.9f, 1.0f},
                                   .text = "OBJECTS IN SYSTEM"});
                const auto objects = starforge::game::sectorObjects(selectedSystem);
                std::size_t row = 0;
                for (const auto& obj : objects)
                {
                    std::string label = obj.kind + ": " + obj.id;
                    if (label.size() > 32) label.resize(32);
                    uiCanvas->addText({.x = ix, .y = is.y + 134.0f +
                                            static_cast<float>(row++) * 16.0f,
                                       .scale = 1.0f,
                                       .color = obj.kind == "hostile"
                                           ? starforge::math::Vec4f{1.0f, 0.4f, 0.3f, 1.0f}
                                           : starforge::math::Vec4f{0.8f, 0.92f, 1.0f, 1.0f},
                                       .text = label});
                }
                const bool jumpHot = uiState.hovered("map-jump");
                uiCanvas->addPanel({.x = layout.jumpButton.x, .y = layout.jumpButton.y,
                                    .w = layout.jumpButton.w, .h = layout.jumpButton.h,
                                    .outlineWidth = 1.5f,
                                    .fillColor = jumpHot
                                        ? starforge::math::Vec4f{0.25f, 0.5f, 0.35f, 1.0f}
                                        : starforge::math::Vec4f{0.12f, 0.24f, 0.38f, 1.0f},
                                    .accentColor = {0.3f, 1.0f, 0.6f, 1.0f}});
                const auto jumpLabel = starforge::game::jumpButtonLabel(
                    selectedSystem, simulation.currentSystem());
                uiCanvas->addText({.x = layout.jumpButton.x + 14.0f,
                                   .y = layout.jumpButton.y + 18.0f, .scale = 1.0f,
                                   .color = {0.95f, 0.98f, 1.0f, 1.0f}, .text = jumpLabel});
            }

            // M16.9: clickable pause menu with Quit.
            if (flight.paused)
            {
                const float px = static_cast<float>(size.width) * 0.5f - 110.0f;
                const float py = static_cast<float>(size.height) * 0.5f - 80.0f;
                uiCanvas->addPanel({.x = px, .y = py, .w = 220.0f, .h = 240.0f,
                                    .outlineWidth = 2.0f,
                                    .fillColor = {0.06f, 0.09f, 0.16f, 0.95f},
                                    .accentColor = {0.95f, 0.7f, 0.2f, 1.0f}});
                uiCanvas->addText({.x = px + 18.0f, .y = py + 14.0f, .scale = 3.0f,
                                   .color = {1.0f, 0.9f, 0.5f, 1.0f}, .text = "PAUSED"});
                for (const auto& button : starforge::game::menuButtons())
                {
                    const bool hot = uiState.hovered(button.id);
                    uiCanvas->addPanel({.x = px + button.x, .y = py + button.y,
                                        .w = button.w, .h = button.h, .outlineWidth = 1.5f,
                                        .fillColor = hot
                                            ? starforge::math::Vec4f{0.2f, 0.45f, 0.3f, 1.0f}
                                            : starforge::math::Vec4f{0.1f, 0.2f, 0.32f, 1.0f}});
                    uiCanvas->addText({.x = px + button.x + 10.0f,
                                       .y = py + button.y + 10.0f, .scale = 2.0f,
                                       .color = {0.95f, 0.98f, 1.0f, 1.0f},
                                       .text = button.label});
                }
                uiCanvas->addText({.x = px + 14.0f, .y = py + 216.0f, .scale = 1.0f,
                                   .color = {0.6f, 0.75f, 0.85f, 1.0f},
                                   .text = "P/Esc resume, T lock, M map"});
            }
            uiCanvas->record(fc);

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
