#include <doctest/doctest.h>

#include "ai/components.hpp"
#include "ai/perception.hpp"
#include "ai/scheduler.hpp"
#include "ecs/world.hpp"

using namespace starforge::ai;
using namespace starforge::ecs;
using namespace starforge::spatial;

TEST_CASE("AI schedule: one think pass processes a deterministic budget subset")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    std::vector<flecs::entity> ships;
    for (std::uint32_t i = 0; i < 50; ++i)
    {
        auto position = starforge::spatial::WorldPosition::fromMeters(0.0, static_cast<double>(i) * 10.0, 0.0);
        REQUIRE(position.has_value());
        ships.push_back(createNpcShip(world, *position, 1));
    }
    (void)createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    AiThinkSchedule schedule;
    schedule.maxAgentsPerTick = 12;
    schedule.thinkIntervalSeconds = 0.1;

    tickAiThink(world, index, 1.0 / 60.0, schedule, BehaviorParams{});

    std::uint32_t engaged = 0;
    for (const auto ship : ships)
    {
        if (ship.get<AiAgent>().state == BehaviorState::Engaging)
        {
            ++engaged;
        }
    }
    CHECK(engaged > 0);
    CHECK(engaged <= schedule.maxAgentsPerTick);
    CHECK(world.entityCount() == 51);
}

TEST_CASE("AI schedule: agents eventually all think again on later frames")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    for (std::uint32_t i = 0; i < 50; ++i)
    {
        auto position = starforge::spatial::WorldPosition::fromMeters(0.0, static_cast<double>(i) * 10.0, 0.0);
        REQUIRE(position.has_value());
        (void)createNpcShip(world, *position, 1);
    }
    (void)createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    AiThinkSchedule schedule;
    schedule.maxAgentsPerTick = 12;
    schedule.thinkIntervalSeconds = 0.1;

    for (int frame = 0; frame < 10; ++frame)
    {
        index.rebuild();
        tickAiThink(world, index, 1.0 / 60.0, schedule, BehaviorParams{});
    }

    std::uint32_t engaged = 0;
    world.world().query<AiAgent>().each([&engaged](flecs::entity, const AiAgent& agent) {
        if (agent.state == BehaviorState::Engaging)
        {
            ++engaged;
        }
    });
    // The hostile pirate engages too, so the full factioned component set
    // reaches the engaged state.
    CHECK(engaged == 51);
}

TEST_CASE("AI schedule: budget overflow rolls over instead of starving")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    // Three agents with a budget of one per tick: the two that miss the
    // first tick must stay due and think on later ticks.
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto position = starforge::spatial::WorldPosition::fromMeters(0.0, static_cast<double>(i) * 10.0, 0.0);
        REQUIRE(position.has_value());
        (void)createNpcShip(world, *position, 1);
    }
    (void)createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    AiThinkSchedule schedule;
    schedule.maxAgentsPerTick = 1;
    schedule.thinkIntervalSeconds = 60.0;

    for (int frame = 0; frame < 4; ++frame)
    {
        index.rebuild();
        tickAiThink(world, index, 1.0 / 60.0, schedule, BehaviorParams{});
    }

    std::uint32_t engaged = 0;
    world.world().query<AiAgent>().each([&engaged](flecs::entity, const AiAgent& agent) {
        if (agent.state == BehaviorState::Engaging)
        {
            ++engaged;
        }
    });
    CHECK(engaged == 4);
}

TEST_CASE("AI schedule: FSM timers run at real simulation speed")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto dockPos = starforge::spatial::WorldPosition::fromMeters(50.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(dockPos.has_value());

    // Park the ship exactly on its dock destination so the FSM's Docking
    // state is pure dwell-timer behaviour.
    const auto ship = createNpcShip(world, *dockPos, 1);
    auto& agent = ship.get_mut<AiAgent>();
    agent.state = BehaviorState::Docking;
    NavigationTarget navigation;
    navigation.positionHint = *dockPos;
    navigation.arrivalRadius = 10.0f;
    ship.set<NavigationTarget>(navigation);

    SpatialIndex index(world);
    index.rebuild();

    AiThinkSchedule schedule;
    schedule.maxAgentsPerTick = 100;
    schedule.thinkIntervalSeconds = 0.1;

    // 1 simulation second of fixed ticks. With the timer-dilution bug
    // (stateTimer advanced by single-frame dt once per think tick),
    // stateTimer would read ~0.1 after a second; at real speed it reads
    // ~1.0. (We assert the timer, not a state exit: once the dwell
    // completes the FSM goes Transit -> Docking again on its intact
    // NavigationTarget, which advanceRoute normally resolves first.)
    BehaviorParams params;
    params.dockDwellSeconds = 2.0f;
    for (int frame = 0; frame < 60; ++frame)
    {
        index.rebuild();
        tickAiThink(world, index, 1.0 / 60.0, schedule, params);
    }
    CHECK(ship.get<AiAgent>().stateTimer == doctest::Approx(1.0).epsilon(0.02));
}

TEST_CASE("AI schedule: steering integrates over think interval, not tick dt")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(200.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    auto& agent = ship.get_mut<AiAgent>();
    agent.maxSpeed = 100.0f;
    agent.maxAcceleration = 50.0f;
    (void)createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    AiThinkSchedule schedule;
    schedule.maxAgentsPerTick = 100;
    schedule.thinkIntervalSeconds = 0.1;

    tickAiThink(world, index, 1.0 / 60.0, schedule, BehaviorParams{});
    REQUIRE(ship.has<starforge::ecs::Velocity>());
    const auto velocity = ship.get<starforge::ecs::Velocity>().linear;
    // First think integrates the full 0.1 s think interval of
    // acceleration (50 m/s^2 * 0.1 s = 5 m/s). Passing the single-tick
    // dt (1/60) would give ~0.83 m/s instead.
    CHECK(velocity.x == doctest::Approx(5.0).epsilon(0.05));
}

TEST_CASE("AI schedule: snapshot invalidates when agents are added or destroyed")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    std::vector<flecs::entity> ships;
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto position = starforge::spatial::WorldPosition::fromMeters(0.0, static_cast<double>(i) * 10.0, 0.0);
        REQUIRE(position.has_value());
        ships.push_back(createNpcShip(world, *position, 1));
    }
    (void)createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    AiThinkSchedule schedule;
    schedule.maxAgentsPerTick = 100;
    schedule.thinkIntervalSeconds = 60.0;

    tickAiThink(world, index, 1.0 / 60.0, schedule, BehaviorParams{});
    CHECK(schedule.snapshotValid);
    CHECK(schedule.agentBuffer.size() == 4);

    // A late-added agent must join the snapshot on the next tick.
    const auto latePos = starforge::spatial::WorldPosition::fromMeters(0.0, 30.0, 0.0);
    REQUIRE(latePos.has_value());
    const auto lateShip = createNpcShip(world, *latePos, 1);
    index.rebuild();
    tickAiThink(world, index, 1.0 / 60.0, schedule, BehaviorParams{});
    CHECK(schedule.agentBuffer.size() == 5);
    CHECK(lateShip.get<AiAgent>().state == BehaviorState::Engaging);

    // Destroying a cached agent invalidates the snapshot; the survivors
    // must keep thinking without touching the dead entry.
    ships.front().destruct();
    index.rebuild();
    tickAiThink(world, index, 1.0 / 60.0, schedule, BehaviorParams{});
    CHECK(schedule.agentBuffer.size() == 4);

    std::uint32_t engaged = 0;
    world.world().query<AiAgent>().each([&engaged](flecs::entity, const AiAgent& agent) {
        if (agent.state == BehaviorState::Engaging)
        {
            ++engaged;
        }
    });
    CHECK(engaged == 4);
}
