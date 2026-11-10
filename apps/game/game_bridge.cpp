#include "game_bridge.hpp"

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

#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>

namespace starforge::game
{

namespace
{

using CellKey = std::tuple<std::int64_t, std::int64_t, std::int64_t, int>;

[[nodiscard]] CellKey cellKey(const content::CellCoord& cell) noexcept
{
    return {cell.x, cell.y, cell.z, cell.level};
}

// Spawn just west of the first cell boundary (2^20 = 1_048_576 m) so
// flying east crosses into cell 1 within a typical demo session.
inline constexpr double kSpawnX = 1047000.0;
inline constexpr std::uint64_t kFieldSeed = 42;

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

    Impl()
        : start(spatial::WorldPosition::fromMeters(kSpawnX, 0.0, 0.0).value()),
          ship(ecs.createShip(start, {0.0, 0.0, 0.0}))
    {
        for (std::uint64_t i = 0; i < 220; ++i)
        {
            // Ball around spawn (all headings) so the field is dense
            // wherever the player turns.
            const double x = 1047000.0 + (hash01(1000 + i) - 0.5) * 2400.0;
            const double y = (hash01(2000 + i) - 0.5) * 2400.0;
            const double z = (hash01(3000 + i) - 0.5) * 2400.0;
            // Keep a 150 m clear sphere around spawn so the opening
            // sightline isn't inside a rock.
            const double dx = x - 1047000.0;
            if (dx * dx + y * y + z * z < 150.0 * 150.0)
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
            spin.angular = math::Vec3f{(static_cast<float>(hash01(7000 + i)) - 0.5f) * 0.8f,
                                       (static_cast<float>(hash01(8000 + i)) - 0.5f) * 0.8f,
                                       (static_cast<float>(hash01(9000 + i)) - 0.5f) * 0.8f};
            entity.set<ecs::Velocity>(spin);
            authored.push_back(entity);
        }
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
    return hit;
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
