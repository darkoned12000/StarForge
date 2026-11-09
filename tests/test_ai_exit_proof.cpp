#include <doctest/doctest.h>

#include "ai/behavior.hpp"
#include "ai/combat.hpp"
#include "ai/components.hpp"
#include "ai/scheduler.hpp"
#include "ai/steering.hpp"
#include "ai/traffic.hpp"
#include "ecs/world.hpp"
#include "jobs/job_scheduler.hpp"
#include "physics/bridge.hpp"
#include "physics/physics_world.hpp"
#include "spatial/camera_relative.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using namespace starforge::ai;
namespace jobs = starforge::jobs;
namespace spatial = starforge::spatial;
namespace ecs = starforge::ecs;
using namespace starforge::physics;

namespace
{

struct ScenarioResult
{
    std::vector<spatial::WorldPosition> finalPositions;
    int routesCompleted = 0;
    int engagements = 0;
    int damageEvents = 0;
    bool collisionSeen = false;
    double p95Ms = 0.0;
};

[[nodiscard]] ScenarioResult runScenario()
{
    constexpr int kFrames = 600;
    constexpr double kDt = 1.0 / 60.0;

    PhysicsWorld physicsWorld(PhysicsWorld::Config{});
    ecs::Ecs ecsWorld;
    registerAiComponents(ecsWorld);

    const auto camera = spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camera.has_value());

    const auto floor = physicsWorld.createStaticBox(700.0f, 1.0f, 700.0f, 0.0f, -6.0f, 0.0f);
    REQUIRE(floor.isValid());

    const auto stationPos = spatial::WorldPosition::fromMeters(200.0, 0.0, 0.0);
    REQUIRE(stationPos.has_value());
    const auto station = ecsWorld.createStation(*stationPos);

    TrafficManager traffic;
    std::vector<flecs::entity> ships;
    ships.reserve(50);

    const auto addShip = [&](double x, double y, std::uint32_t faction) {
        const auto pos = spatial::WorldPosition::fromMeters(x, y, 0.0);
        REQUIRE(pos.has_value());
        const auto ship = createNpcShip(ecsWorld, *pos, faction);
        auto& agent = ship.get_mut<AiAgent>();
        agent.maxSpeed = 100.0f;
        agent.maxAcceleration = 200.0f;
        agent.perceptionRadius = 1000.0f;
        ship.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
        REQUIRE(attachBody(ecsWorld, ship, physicsWorld, *camera).isValid());
        ship.set<Weapon>({500.0f, 5.0f, 1.0f, 0.0f});
        ships.push_back(ship);
        return ship;
    };

    // 25 merchants start at the west hub and route to the station.
    std::vector<flecs::entity> merchants;
    for (std::uint32_t i = 0; i < 25; ++i)
    {
        const auto ship = addShip(-200.0 + static_cast<double>(i) * 5.0, -50.0 + static_cast<double>(i) * 4.0, 1);
        merchants.push_back(ship);
        const auto slot = traffic.reserveDockSlot(station, ship, 32);
        REQUIRE(slot.has_value());
        assignRoute(ship, spatial::WorldPosition::fromMeters(-200.0 + static_cast<double>(i) * 5.0,
                                                            -50.0 + static_cast<double>(i) * 4.0, 0.0)
                                .value(),
                    *stationPos, slot.value(), station);
    }

    // 25 pirates start east of the merchants and west of the station.
    for (std::uint32_t i = 0; i < 25; ++i)
    {
        (void)addShip(300.0 - static_cast<double>(i) * 5.0, -50.0 + static_cast<double>(i) * 4.0, 0);
    }

    registerBodyCleanup(ecsWorld, physicsWorld);
    ecs::SpatialIndex index(ecsWorld);
    AiThinkSchedule schedule;
    BehaviorParams behaviorParams;

    ScenarioResult result;
    std::vector<double> timings;
    timings.reserve(kFrames);

    for (int frame = 0; frame < kFrames; ++frame)
    {
        const auto frameStart = std::chrono::steady_clock::now();

        index.rebuild();
        tickAiThink(ecsWorld, index, kDt, schedule, behaviorParams);
        for (const auto ship : ships)
        {
            (void)advanceRoute(ecsWorld, ship, kDt, index, &traffic);
        }
        tickWeapons(ecsWorld, kDt);

        for (const auto ship : ships)
        {
            if (!ship.has<AiAgent>())
            {
                continue;
            }
            const auto agent = ship.get<AiAgent>();
            if (agent.target.is_alive() && agent.target.has<ecs::Transform>())
            {
                (void)tryFire(ecsWorld, physicsWorld, ship, agent.target, *camera);
            }
            if (ship.has<ecs::Velocity>())
            {
                const auto velocity = ship.get<ecs::Velocity>().linear;
                if (ship.has<PhysicsBody>())
                {
                    physicsWorld.setLinearVelocity(ship.get<PhysicsBody>().body,
                                                   {static_cast<float>(velocity.x),
                                                    static_cast<float>(velocity.y),
                                                    static_cast<float>(velocity.z)});
                }
            }
        }

        // Damage/fire events are single-tick payloads: count them now and
        // clear them so later frames never observe stale hits.
        for (const auto ship : ships)
        {
            if (ship.has<DamageEvent>())
            {
                ++result.damageEvents;
            }
        }
        clearCombatEvents(ecsWorld);

        physicsWorld.step(1.0f / 60.0f);
        syncPhysicsToEcs(ecsWorld, physicsWorld, *camera);
        physicsWorld.flushContacts(ecsWorld, *camera);

        const auto frameEnd = std::chrono::steady_clock::now();
        timings.push_back(std::chrono::duration<double, std::milli>(frameEnd - frameStart).count());
    }

    std::sort(timings.begin(), timings.end());
    result.p95Ms = timings[(timings.size() * 95) / 100];

    for (const auto ship : ships)
    {
        result.finalPositions.push_back(ship.get<ecs::Transform>().position);
        if (const auto route = ship.has<NavigationRoute>() ? std::optional<const NavigationRoute>(ship.get<NavigationRoute>())
                                                            : std::nullopt)
        {
            if (route->complete)
            {
                ++result.routesCompleted;
            }
        }
        const auto agent = ship.get<AiAgent>();
        if (agent.state == BehaviorState::Engaging)
        {
            ++result.engagements;
        }
        if (ship.has<ecs::CollisionEvent>())
        {
            result.collisionSeen = true;
        }
    }

    return result;
}

} // namespace

TEST_CASE("AI exit proof: two deterministic runs of a 50-ship simulation match")
{
    jobs::initGlobalScheduler();

    const auto first = runScenario();
    const auto second = runScenario();

    REQUIRE(first.finalPositions.size() == second.finalPositions.size());
    REQUIRE(first.finalPositions.size() == 50);
    for (std::size_t i = 0; i < first.finalPositions.size(); ++i)
    {
        const auto& a = first.finalPositions[i];
        const auto& b = second.finalPositions[i];
        CHECK(a.cellX == b.cellX);
        CHECK(a.cellY == b.cellY);
        CHECK(a.cellZ == b.cellZ);
        CHECK(a.offsetX == doctest::Approx(b.offsetX));
        CHECK(a.offsetY == doctest::Approx(b.offsetY));
        CHECK(a.offsetZ == doctest::Approx(b.offsetZ));
    }

    CHECK(first.p95Ms < 6.94); // p95 frame budget at 60 fps
    CHECK(first.engagements > 0);
    CHECK(first.damageEvents > 0);
    // The traffic lanes keep the simulation collision-free in this
    // configuration; damage events prove combat interactions were resolved.
    CHECK(!first.collisionSeen);

    jobs::shutdownGlobalScheduler();
}
