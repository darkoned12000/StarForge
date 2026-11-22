#include "game_bridge.hpp"

#include "content/asteroid_field.hpp"
#include "content/cell_traversal.hpp"
#include "content/chunk_cache.hpp"
#include "content/upload_budget.hpp"
#include "ecs/content_instantiation.hpp"
#include "ecs/components.hpp"
#include "ecs/world.hpp"
#include "math/math.hpp"
#include "spatial/camera_relative.hpp"
#include "spatial/world_position.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace starforge::game
{

namespace
{

using CellKey = std::tuple<std::int64_t, std::int64_t, std::int64_t, int>;
// Key of one fine-grained procedural field cell (metres, not the
// astronomical 2^20 m hierarchy).
struct FieldCellKey
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    bool operator==(const FieldCellKey&) const = default;

    bool operator<(const FieldCellKey& other) const noexcept
    {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
};

[[nodiscard]] CellKey cellKey(const content::CellCoord& cell) noexcept
{
    return {cell.x, cell.y, cell.z, cell.level};
}

// Spawn just west of the first cell boundary (2^20 = 1_048_576 m) so
// flying east crosses into cell 1 within a typical demo session.
inline constexpr double kSpawnX = 1047000.0;
inline constexpr std::uint64_t kFieldSeed = 42;

// The procedural rock field is generated in fine world-space cells around
// the ship, so the debris follows the player instead of being a fixed
// cluster at spawn. Rocks dissolve over the outer band rather than
// appearing, which is what keeps the field from popping.
inline constexpr double kRockCellSize = 256.0;
// Rocks are fully solid inside this radius and dissolve to nothing by
// kRockFadeEnd, so the boundary of the loaded field is never visible.
//
// The band has to line up with the draw budget: every rock inside
// kRockFadeEnd must fit in the instance buffer, otherwise the surplus is
// culled by distance and pops back in as the ship flies. At the field
// density below, kRockFadeEnd holds roughly 8000 rocks, comfortably inside
// the 16384 instance budget.
//
// The band starts where the detailed mesh ends (kRockLodDistance), so a
// rock crosses from "flat impostor" to "cratered mesh" exactly as it
// becomes fully solid - no visible pop at the LOD boundary.
inline constexpr double kRockFadeStart = 900.0;
inline constexpr double kRockFadeEnd = 2200.0;
// Cells are kept loaded slightly beyond the fade band so a rock that is
// still fading out does not lose its cell before it has dissolved.
inline constexpr double kRockKeepRadius = 2700.0;
// Distance beyond which a rock is not worth drawing at all.
inline constexpr double kRockDrawRadius = 2200.0;
// Field rocks carry this bit in their render seed so the damage path can
// tell them apart from ECS entities (whose seed is a small entity index).
inline constexpr std::uint32_t kFieldRockSeedBit = 0x8000'0000u;

[[nodiscard]] double hash01(std::uint64_t x) noexcept
{
    // SplitMix64 mixer: strong avalanche so neighbouring seeds don't
    // produce neighbouring outputs (plain LCG left the demo cluster
    // correlated into a visible line).
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x = x ^ (x >> 31);
    return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
}

} // namespace

struct GameWorld::Impl
{
    ecs::Ecs ecs;
    spatial::WorldPosition start;
    flecs::entity ship;

    content::StreamingTraversal stream{5000.0};
    content::RawGenerationStore cache{
        content::RawGenerationStore::Config{.maxEntries = 128, .maxBytes = 8 * 1024 * 1024}};
    content::UploadBudget uploads;
    content::GenerationBudget budget = uploads.budget();
    std::map<CellKey, ecs::MountedChunk> mounted;
    std::vector<content::CellCoord> pendingLoads;

    // Authored near-field cluster (layered generation, M15): spans the
    // cell boundary so the player sees rocks streak past. Procedural
    // chunks are the foundation; this cluster is the demo's authored
    // foreground until at-scale streaming detail lands.
    std::vector<flecs::entity> authored;
    // Remaining hull per asteroid seed (laser damage; M16 refinement).
    std::map<std::uint32_t, float> asteroidHealth;

    // Procedural world-space rock field. The rocks are a pure function of
    // their cell, so the debris follows the ship wherever it flies and the
    // same rock keeps the same identity (shape, tint, damage state) for
    // the whole session.
    content::AsteroidField field{
        content::AsteroidField::Config{
            .worldSeed = kFieldSeed,
            .cellSize = kRockCellSize,
            .rocksPerCell = 3.0f,
            .minScale = 3.0f,
            .maxScale = 26.0f,
            .fadeStart = kRockFadeStart,
            .fadeEnd = kRockFadeEnd,
        }};
    // Generated samples per field cell, cached so a cell is only rolled
    // once no matter how often the ship passes it.
    std::map<FieldCellKey, std::vector<content::AsteroidSample>> cellCache;
    // Samples of the cells currently around the ship.
    std::vector<content::AsteroidSample> activeRocks;
    // Ship position the active set was last built for.
    double activeX = 0.0;
    double activeY = 0.0;
    double activeZ = 0.0;
    bool activeValid = false;
    // Field rocks the player has destroyed, by stable seed.
    std::set<std::uint32_t> destroyedRocks;
    // Accumulated flight time, used to tumble the field rocks.
    double fieldTime = 0.0;

    Impl()
        : start(spatial::WorldPosition::fromMeters(kSpawnX, 0.0, 0.0).value()),
          ship(ecs.createShip(start, {0.0, 0.0, 0.0}))
    {
        for (std::uint64_t i = 0; i < 140; ++i)
        {
            // Dense authored foreground around spawn so the opening shot
            // reads as a debris cloud; the procedural field takes over
            // beyond it.
            const double x = 1047000.0 + (hash01(1000 + i) - 0.5) * 2200.0;
            const double y = (hash01(2000 + i) - 0.5) * 2200.0;
            const double z = (hash01(3000 + i) - 0.5) * 2200.0;
            // Keep a 150 m clear sphere around spawn so the opening
            // sightline isn't inside a rock.
            const double dx = x - 1047000.0;
            if (dx * dx + y * y + z * z < 80.0 * 80.0)
            {
                continue;
            }
            auto pos =
                spatial::WorldPosition::fromMeters(x, y, z);
            if (!pos.has_value())
            {
                continue;
            }
            auto entity = ecs.createAsteroid(*pos, {});
            auto& transform = entity.get_mut<ecs::Transform>();
            // Tumbling rocks + random pose (ECS step integrates).
            // Full yaw*pitch pose: yaw about Y, pitch about X.
            const float yawA = static_cast<float>(hash01(5000 + i)) * 6.2831855f;
            const float pitchA = static_cast<float>(hash01(6000 + i)) * 6.2831855f;
            const glm::quat yawQ =
                glm::angleAxis(yawA, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::quat pitchQ =
                glm::angleAxis(pitchA, glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::quat orientation = yawQ * pitchQ;
            transform.orientation = math::Vec4f{orientation.x, orientation.y, orientation.z,
                                                orientation.w};
            transform.scale = 5.0f + static_cast<float>(hash01(4000 + i)) * 20.0f;
            ecs::Velocity spin;
            spin.linear = math::Vec3d{0.0, 0.0, 0.0};
            // 80% slower tumble than the original 0.8 rad/s spread.
            spin.angular = math::Vec3f{(static_cast<float>(hash01(7000 + i)) - 0.5f) * 0.16f,
                                       (static_cast<float>(hash01(8000 + i)) - 0.5f) * 0.16f,
                                       (static_cast<float>(hash01(9000 + i)) - 0.5f) * 0.16f};
            entity.set<ecs::Velocity>(spin);
            authored.push_back(entity);
        }

        // A small demo cluster directly ahead of the spawn so the player has
        // something to shoot at while learning the weapons (the random field
        // leaves a clear sphere and offers no guaranteed forward target).
        for (const auto& offset : {std::array<double, 3>{0.0, 0.0, -300.0},
                                   std::array<double, 3>{-45.0, 25.0, -420.0},
                                   std::array<double, 3>{55.0, -30.0, -520.0}})
        {
            auto pos = spatial::WorldPosition::fromMeters(
                kSpawnX + offset[0], offset[1], offset[2]);
            if (!pos.has_value()) continue;
            auto entity = ecs.createAsteroid(*pos, {});
            entity.get_mut<ecs::Transform>().scale = 13.0f;
            authored.push_back(entity);
        }
    }

    // Rebuild the set of field cells around the ship.
    //
    // The field is world-anchored and deterministic, so this only has to
    // cover the cells the ship can currently see. Cells are cached by
    // coordinate: flying back over old ground reuses the same samples, so
    // a rock never changes identity or appearance between visits.
    void refreshField(double shipX, double shipY, double shipZ)
    {
        const double span = kRockKeepRadius / kRockCellSize;
        const auto cx = field.cellIndex(shipX);
        const auto cy = field.cellIndex(shipY);
        const auto cz = field.cellIndex(shipZ);

        activeRocks.clear();
        for (std::int64_t dz = -static_cast<std::int64_t>(span);
             dz <= static_cast<std::int64_t>(span); ++dz)
        {
            for (std::int64_t dy = -static_cast<std::int64_t>(span);
                 dy <= static_cast<std::int64_t>(span); ++dy)
            {
                for (std::int64_t dx = -static_cast<std::int64_t>(span);
                     dx <= static_cast<std::int64_t>(span); ++dx)
                {
                    const FieldCellKey key{cx + dx, cy + dy, cz + dz};
                    // Skip cells that are entirely outside the keep radius:
                    // a cell's nearest corner is the closest it can get.
                    if (field.cellDistanceSquared(shipX, shipY, shipZ, key.x, key.y, key.z) >
                        kRockKeepRadius * kRockKeepRadius)
                    {
                        continue;
                    }
                    auto cached = cellCache.find(key);
                    if (cached == cellCache.end())
                    {
                        cached = cellCache.emplace(key, std::vector<content::AsteroidSample>{}).first;
                        field.generateCell(key.x, key.y, key.z, cached->second);
                    }
                    for (const auto& sample : cached->second)
                    {
                        if (destroyedRocks.contains(sample.seed))
                        {
                            continue;
                        }
                        activeRocks.push_back(sample);
                    }
                }
            }
        }

        // Drop cached cells the ship has left far behind, so the cache does
        // not grow without bound over a long session.
        for (auto it = cellCache.begin(); it != cellCache.end();)
        {
            const bool far = field.cellDistanceSquared(shipX, shipY, shipZ, it->first.x,
                                                       it->first.y, it->first.z) >
                              kRockKeepRadius * kRockKeepRadius * 4.0;
            it = far ? cellCache.erase(it) : std::next(it);
        }

        activeX = shipX;
        activeY = shipY;
        activeZ = shipZ;
        activeValid = true;
    }
};

GameWorld::GameWorld() : impl_(std::make_unique<Impl>()) {}

GameWorld::~GameWorld() = default;

GameWorld::GameWorld(GameWorld&& other) noexcept : impl_(std::move(other.impl_)) {}

GameWorld& GameWorld::operator=(GameWorld&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void GameWorld::setVelocity(const RawVec3d& linear, const RawVec3f& angular)
{
    if (!impl_)
    {
        return;
    }
    ecs::Velocity velocity;
    velocity.linear = math::Vec3d{linear.x, linear.y, linear.z};
    velocity.angular = math::Vec3f{angular.x, angular.y, angular.z};
    impl_->ship.set<ecs::Velocity>(velocity);
}

void GameWorld::step(float dt)
{
    if (impl_)
    {
        impl_->ecs.step(dt);
        // Advance the field tumble clock so the procedural rocks rotate.
        impl_->fieldTime += static_cast<double>(dt);
    }
}

RawTransform GameWorld::shipTransform() const
{
    if (!impl_)
    {
        return {};
    }
    const auto& transform = impl_->ship.get<ecs::Transform>();
    const auto metres = transform.position.toMeters();
    return RawTransform{
        .position = RawVec3d{metres.x, metres.y, metres.z},
        .orientation = RawVec4f{transform.orientation.x, transform.orientation.y,
                                 transform.orientation.z, transform.orientation.w},
        .scale = transform.scale,
    };
}

bool GameWorld::collideShip()
{
    if (!impl_)
    {
        return false;
    }
    bool hit = false;
    auto& shipTransform = impl_->ship.get_mut<ecs::Transform>();
    auto check = [&](flecs::entity entity) {
        if (!entity.is_alive() || !entity.has<ecs::Transform>())
        {
            return;
        }
        const auto& rock = entity.get<ecs::Transform>();
        const auto rel = spatial::toCameraRelative(shipTransform.position, rock.position);
        const double dSq = rel.x * rel.x + rel.y * rel.y + rel.z * rel.z;
        // Bounding sphere of the rendered CUBE (half-diagonal), not
        // half-side — corners would otherwise clip through the ship.
        const double radii = static_cast<double>(rock.scale) * 0.866 + 2.0; // rock + ship 2 m
        if (dSq >= radii * radii)
        {
            return;
        }
        hit = true;
        const double dist = std::sqrt(dSq);
        const double push = radii - dist;
        // rel points from rock to ship (ship - rock); push outward.
        math::Vec3d away;
        if (dist > 1e-3)
        {
            away = math::Vec3d{rel.x / dist, rel.y / dist, rel.z / dist};
        }
        else if (impl_->ship.has<ecs::Velocity>())
        {
            const auto& vel = impl_->ship.get<ecs::Velocity>();
            const double len = std::sqrt(vel.linear.x * vel.linear.x + vel.linear.y * vel.linear.y +
                                         vel.linear.z * vel.linear.z);
            away = len > 1e-9 ? math::Vec3d{-vel.linear.x / len, -vel.linear.y / len, -vel.linear.z / len}
                             : math::Vec3d{1.0, 0.0, 0.0};
        }
        else
        {
            away = math::Vec3d{1.0, 0.0, 0.0};
        }
        if (auto moved = shipTransform.position.translated(away * push); moved.has_value())
        {
            shipTransform.position = *moved;
        }
        // Dampen: cancel the velocity component into the rock so the
        // ship slides instead of jitter-clipping every tick.
        if (impl_->ship.has<ecs::Velocity>())
        {
            auto& velocity = impl_->ship.get_mut<ecs::Velocity>();
            const double vn = velocity.linear.x * away.x + velocity.linear.y * away.y +
                              velocity.linear.z * away.z;
            if (vn < 0.0)
            {
                velocity.linear.x -= away.x * vn;
                velocity.linear.y -= away.y * vn;
                velocity.linear.z -= away.z * vn;
            }
        }
    };
    for (const auto entity : impl_->authored)
    {
        check(entity);
    }
    for (const auto& [key, mounted] : impl_->mounted)
    {
        (void)key;
        for (const auto entity : mounted.entities)
        {
            check(entity);
        }
    }

    // Field rocks are plain data, so they need their own collision test.
    const auto shipMetres = impl_->ship.get<ecs::Transform>().position.toMeters();
    for (const auto& sample : impl_->activeRocks)
    {
        const double dx = sample.x - shipMetres.x;
        const double dy = sample.y - shipMetres.y;
        const double dz = sample.z - shipMetres.z;
        const double dSq = dx * dx + dy * dy + dz * dz;
        // Ship radius plus the rock's own radius.
        const double radii = sample.scale + 2.0;
        if (dSq >= radii * radii)
        {
            continue;
        }
        hit = true;
        const double dist = std::sqrt(dSq);
        const double push = radii - dist;
        math::Vec3d away{1.0, 0.0, 0.0};
        if (dist > 1e-3)
        {
            away = math::Vec3d{-dx / dist, -dy / dist, -dz / dist};
        }
        if (auto moved = shipTransform.position.translated(away * push); moved.has_value())
        {
            shipTransform.position = *moved;
        }
    }
    return hit;
}

bool GameWorld::damageAsteroid(std::uint32_t seed, float damage,
                               RawVec3d* outPosition, float* outScale)
{
    if (!impl_) return false;

    // Field rocks are pure data, not entities, so they take their own path:
    // the seed identifies the rock directly and destruction just marks it
    // gone in the destroyed set.
    if ((seed & kFieldRockSeedBit) != 0)
    {
        const std::uint32_t fieldSeed = seed & ~kFieldRockSeedBit;
        for (const auto& sample : impl_->activeRocks)
        {
            if (sample.seed != fieldSeed) continue;
            auto& health = impl_->asteroidHealth[seed];
            if (health <= 0.0f) health = 30.0f + (fieldSeed % 7) * 6.0f;
            health -= damage;
            if (health > 0.0f) return false;
            impl_->asteroidHealth.erase(seed);
            impl_->destroyedRocks.insert(fieldSeed);
            if (outPosition)
            {
                const auto shipMetres =
                    impl_->ship.get<ecs::Transform>().position.toMeters();
                *outPosition = RawVec3d{sample.x - shipMetres.x, sample.y - shipMetres.y,
                                         sample.z - shipMetres.z};
            }
            if (outScale) *outScale = sample.scale;
            return true;
        }
        return false;
    }

    flecs::entity target;
    auto scan = [&](flecs::entity entity) {
        if (target.is_alive() || !entity.is_alive() || !entity.has<ecs::Transform>()) return;
        if (static_cast<std::uint32_t>(entity.raw_id()) == seed) target = entity;
    };
    for (const auto entity : impl_->authored) scan(entity);
    for (const auto& [key, mounted] : impl_->mounted)
    {
        (void)key;
        for (const auto entity : mounted.entities) scan(entity);
    }
    if (!target.is_alive()) return false;

    auto& health = impl_->asteroidHealth[seed];
    if (health <= 0.0f) health = 30.0f + (seed % 7) * 6.0f; // seed-scaled toughness
    health -= damage;
    if (health > 0.0f) return false;

    const auto& t = target.get<ecs::Transform>();
    if (outPosition)
    {
        const auto rel = spatial::toCameraRelative(t.position,
            impl_->ship.get<ecs::Transform>().position);
        *outPosition = RawVec3d{rel.x, rel.y, rel.z};
    }
    if (outScale) *outScale = t.scale;
    impl_->asteroidHealth.erase(seed);
    impl_->authored.erase(
        std::remove(impl_->authored.begin(), impl_->authored.end(), target),
        impl_->authored.end());
    target.destruct();
    return true;
}

int GameWorld::updateStreaming()
{
    if (!impl_)
    {
        return 0;
    }
    // Per-frame budget: without this, streaming stops permanently
    // after the first few chunks (review fix).
    impl_->uploads.resetFrame();

    // Refresh the procedural rock field around the ship. It is regenerated
    // from world-space cells, so it is stable across frames: the same rock
    // keeps its position, shape and tint no matter how the player moves.
    {
        const auto& shipTransform = impl_->ship.get<ecs::Transform>();
        const auto metres = shipTransform.position.toMeters();
        const double moved = std::sqrt((metres.x - impl_->activeX) * (metres.x - impl_->activeX) +
                                       (metres.y - impl_->activeY) * (metres.y - impl_->activeY) +
                                       (metres.z - impl_->activeZ) * (metres.z - impl_->activeZ));
        // Only rebuild when the ship has left the current cell, so the
        // per-frame cost stays near zero. Half a cell is enough: at boost
        // speed a full cell is 0.28 s, and the field would visibly lag
        // behind the ship.
        if (!impl_->activeValid || moved > kRockCellSize * 0.5)
        {
            impl_->refreshField(metres.x, metres.y, metres.z);
        }
    }

    int loaded = 0;
    auto tryMount = [&](const content::CellCoord& cell) -> bool {
        if (impl_->mounted.contains(cellKey(cell)))
        {
            return true;
        }
        auto chunk = impl_->cache.get(kFieldSeed, cell, impl_->budget);
        if (chunk == nullptr)
        {
            return false;
        }
        const auto decision =
            impl_->uploads.reserve(content::chunkStagingBytes(*chunk), content::chunkDrawCalls(*chunk));
        if (!decision.accepted)
        {
            return false;
        }
        auto mounted = ecs::instantiateChunk(impl_->ecs, *chunk);
        impl_->mounted[cellKey(cell)] = std::move(mounted);
        ++loaded;
        return true;
    };

    // Retry previously rejected cells first (lost chunks would
    // otherwise never re-enter the traversal).
    {
        std::vector<content::CellCoord> retry;
        for (const auto& cell : impl_->pendingLoads)
        {
            if (!impl_->mounted.contains(cellKey(cell)) && !tryMount(cell))
            {
                retry.push_back(cell);
            }
        }
        impl_->pendingLoads = std::move(retry);
    }

    const auto& shipPos = impl_->ship.get<ecs::Transform>().position;
    auto update = impl_->stream.advance(shipPos, 0, 0, 5000.0);
    for (const auto& cell : update.loaded)
    {
        if (!tryMount(cell))
        {
            impl_->pendingLoads.push_back(cell);
        }
    }
    for (const auto& cell : update.unloaded)
    {
        // Prune evicted cells from pendingLoads too, or the retry
        // queue resurrects out-of-range chunks forever (review fix).
        std::erase_if(impl_->pendingLoads, [&](const content::CellCoord& c) {
            return cellKey(c) == cellKey(cell);
        });
        auto it = impl_->mounted.find(cellKey(cell));
        if (it != impl_->mounted.end())
        {
            ecs::unmountChunk(it->second);
            impl_->mounted.erase(it);
        }
    }
    return loaded;
}

RawVec3d GameWorld::shipCell() const
{
    if (!impl_)
    {
        return {};
    }
    const auto& pos = impl_->ship.get<ecs::Transform>().position;
    return RawVec3d{static_cast<double>(pos.cellX), static_cast<double>(pos.cellY),
                    static_cast<double>(pos.cellZ)};
}

std::vector<RawTransform> GameWorld::visibleAsteroids(double radiusMetres) const
{
    std::vector<RawTransform> result;
    if (!impl_)
    {
        return result;
    }
    const auto& shipPos = impl_->ship.get<ecs::Transform>().position;
    const double rSq = radiusMetres * radiusMetres;
    auto consider = [&](flecs::entity entity) {
        if (!entity.is_alive() || !entity.has<ecs::Transform>())
        {
            return;
        }
        const auto& t = entity.get<ecs::Transform>();
        const auto rel = spatial::toCameraRelative(t.position, shipPos);
        const double dSq = rel.x * rel.x + rel.y * rel.y + rel.z * rel.z;
        if (dSq > rSq)
        {
            return;
        }
        result.push_back(RawTransform{
            .position = RawVec3d{rel.x, rel.y, rel.z},
            .orientation = RawVec4f{t.orientation.x, t.orientation.y, t.orientation.z,
                                     t.orientation.w},
            .scale = t.scale,
            .seed = static_cast<std::uint32_t>(entity.raw_id()),
        });
    };
    for (const auto entity : impl_->authored)
    {
        consider(entity);
    }
    const auto shipMetres = shipPos.toMeters();
    constexpr double kCellSize = spatial::WorldPosition::kCellSize;
    for (const auto& [key, mounted] : impl_->mounted)
    {
        (void)key;
        // Chunk-AABB cull (not bounding-sphere): level-0 cells are
        // 1 M m cubes, so nearest-point distance tells whether any
        // rock inside can fall within the visible radius.
        const double minX = static_cast<double>(mounted.cell.x) * kCellSize;
        const double minY = static_cast<double>(mounted.cell.y) * kCellSize;
        const double minZ = static_cast<double>(mounted.cell.z) * kCellSize;
        const double nearX = std::max(minX, std::min(shipMetres.x, minX + kCellSize));
        const double nearY = std::max(minY, std::min(shipMetres.y, minY + kCellSize));
        const double nearZ = std::max(minZ, std::min(shipMetres.z, minZ + kCellSize));
        const double dx = nearX - shipMetres.x;
        const double dy = nearY - shipMetres.y;
        const double dz = nearZ - shipMetres.z;
        if (dx * dx + dy * dy + dz * dz > rSq)
        {
            continue;
        }
        for (const auto entity : mounted.entities)
        {
            consider(entity);
        }
    }

    // Procedural field rocks. They are pure data (no ECS entity): their
    // position, scale and orientation come straight from the deterministic
    // field, so no per-rock entity churn happens while flying.
    const auto fieldPos = shipPos.toMeters();
    for (const auto& sample : impl_->activeRocks)
    {
        const double dx = sample.x - fieldPos.x;
        const double dy = sample.y - fieldPos.y;
        const double dz = sample.z - fieldPos.z;
        const double dSq = dx * dx + dy * dy + dz * dz;
        if (dSq > rSq)
        {
            continue;
        }
        const double dist = std::sqrt(dSq);
        // Dissolve the rock out over the outer band of the field so it
        // fades away instead of popping into existence at the boundary.
        const double fade = 1.0 - std::clamp((dist - kRockFadeStart) /
                                                  (kRockFadeEnd - kRockFadeStart),
                                              0.0, 1.0);
        if (fade <= 0.0)
        {
            continue;
        }
        // Tumble: spin the rock about its own axis at a constant rate.
        const glm::quat base =
            glm::angleAxis(static_cast<float>(sample.yaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::angleAxis(static_cast<float>(sample.pitch), glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::angleAxis(static_cast<float>(sample.roll), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::vec3 axis{sample.spinAxis[0], sample.spinAxis[1], sample.spinAxis[2]};
        const glm::quat spin =
            glm::angleAxis(static_cast<float>(sample.spinRate * impl_->fieldTime), axis);
        const glm::quat orientation = glm::normalize(spin * base);
        result.push_back(RawTransform{
            .position = RawVec3d{dx, dy, dz},
            .orientation = RawVec4f{orientation.x, orientation.y, orientation.z, orientation.w},
            .scale = sample.scale,
            .seed = sample.seed | kFieldRockSeedBit,
            .fade = static_cast<float>(fade),
        });
    }
    return result;
}

std::size_t GameWorld::mountedAsteroidCount() const
{
    if (!impl_)
    {
        return 0;
    }
    std::size_t total = impl_->authored.size();
    for (const auto& [key, mounted] : impl_->mounted)
    {
        (void)key;
        total += mounted.entities.size();
    }
    return total;
}

std::size_t GameWorld::streamedChunkCount() const
{
    return impl_ ? impl_->mounted.size() : 0;
}

} // namespace starforge::game
