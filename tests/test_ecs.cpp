// ECS scene layer tests (M10.1/10.2).
#include <doctest/doctest.h>

#include "ecs/spatial_index.hpp"
#include "ecs/world.hpp"

#include <cmath>

using namespace starforge::ecs;
using namespace starforge::spatial;

namespace
{

[[nodiscard]] WorldPosition at(double x, double y, double z)
{
    auto position = WorldPosition::fromMeters(x, y, z);
    REQUIRE(position.has_value());
    return *position;
}

[[nodiscard]] bool near(double lhs, double rhs, double epsilon = 1.0e-6)
{
    return std::abs(lhs - rhs) <= epsilon;
}

} // namespace

TEST_CASE("ECS: entities spawn with tags and count")
{
    Ecs world;
    const auto ship = world.createShip(at(0, 0, 0), {1000.0, 0.0, 0.0});
    const auto asteroid = world.createAsteroid(at(10, 0, 0));
    const auto station = world.createStation(at(0, 0, 100));
    const auto projectile = world.createProjectile(at(0, 5, 0), {0.0, 0.0, 2000.0});

    CHECK(ship.is_valid());
    CHECK(asteroid.is_valid());
    CHECK(station.is_valid());
    CHECK(projectile.is_valid());
    CHECK(world.entityCount() == 4);
    CHECK(world.world().count<ShipTag>() == 1);
    CHECK(world.world().count<AsteroidTag>() == 1);
    CHECK(world.world().count<StationTag>() == 1);
    CHECK(world.world().count<ProjectileTag>() == 1);

    // Only moving entities carry a Velocity component.
    CHECK(ship.has<Velocity>());
    CHECK(projectile.has<Velocity>());
    CHECK(!station.has<Velocity>());
    CHECK(!asteroid.has<Velocity>());
}

TEST_CASE("ECS: movement preserves precision at interstellar range")
{
    Ecs world;
    const auto entity = world.createShip(at(1.0e12, 0.0, 0.0), {1000.0, 0.0, 0.0});

    world.step(1.0f);

    const auto transform = entity.get<Transform>();
    const auto moved = transform.position.toMeters();
    CHECK(near(moved.x, 1.0e12 + 1000.0, 1.0e-3));
    CHECK(near(moved.y, 0.0));
    CHECK(near(moved.z, 0.0));

    // Zero dt is a no-op.
    world.step(0.0f);
    CHECK(near(entity.get<Transform>().position.toMeters().x, 1.0e12 + 1000.0, 1.0e-3));
}

TEST_CASE("ECS: orientation integrates under angular velocity")
{
    Ecs world;
    const auto entity = world.createShip(at(0, 0, 0), {0.0, 0.0, 0.0});
    entity.set<Velocity>({.linear = {0.0, 0.0, 0.0}, .angular = {0.0f, 0.0f, 1.0f}});

    world.step(1.0f);

    // Rotated 1 radian about Z: identity (0,0,0,1) -> (0,0,sin(0.5),cos(0.5)).
    const auto orientation = entity.get<Transform>().orientation;
    CHECK(near(orientation.x, 0.0, 1.0e-5));
    CHECK(near(orientation.y, 0.0, 1.0e-5));
    CHECK(near(orientation.z, std::sin(0.5f), 1.0e-5));
    CHECK(near(orientation.w, std::cos(0.5f), 1.0e-5));
}

TEST_CASE("ECS: spatial index buckets by exact cell and tracks movement")
{
    Ecs world;
    const auto nearOrigin = world.createAsteroid(at(1, 2, 3));
    const auto alsoNearOrigin = world.createAsteroid(at(100, 200, 300));
    // Far away: many cells over (cell size is ~1e6 m).
    const auto distant = world.createAsteroid(at(5.0e6, 0, 0), {2.0e6, 0, 0});

    SpatialIndex index(world);
    index.rebuild();
    REQUIRE(index.entityCount() == 3);

    const CellCoord originCell{0, 0, 0};
    const auto* cell = index.findCell(originCell);
    REQUIRE(cell != nullptr);
    CHECK(cell->size() == 2);

    const auto distantPos = at(5.0e6, 0, 0);
    const CellCoord distantCell{distantPos.cellX, distantPos.cellY, distantPos.cellZ};
    const auto* distantBucket = index.findCell(distantCell);
    REQUIRE(distantBucket != nullptr);
    CHECK(distantBucket->size() == 1);
    CHECK((*distantBucket)[0] == distant);

    // Neighborhood query finds both origin-cell entities, no duplicates.
    const auto origin = at(0, 0, 0);
    const auto neighbors = index.findNeighborhood(origin);
    CHECK(neighbors.size() == 2);

    // Move the distant entity across a cell boundary and rebuild.
    world.step(1.0f);
    index.rebuild();
    CHECK(index.findCell(distantCell) == nullptr);
    const auto movedPos = distant.get<Transform>().position;
    const CellCoord movedCell{movedPos.cellX, movedPos.cellY, movedPos.cellZ};
    const auto* movedBucket = index.findCell(movedCell);
    REQUIRE(movedBucket != nullptr);
    CHECK(movedBucket->size() == 1);

    (void)nearOrigin;
    (void)alsoNearOrigin;
}
