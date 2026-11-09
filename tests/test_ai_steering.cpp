#include <doctest/doctest.h>

#include "ai/components.hpp"
#include "ai/steering.hpp"
#include "ecs/world.hpp"

#include <cmath>
#include <vector>

using namespace starforge::ai;
using namespace starforge::ecs;
using namespace starforge::math;
using namespace starforge::spatial;

TEST_CASE("AI steering: arrival braking slows near the target")
{
    SteeringParams params;
    params.maxSpeed = 20.0;
    params.maxAcceleration = 10.0;
    params.arrivalRadius = 5.0;

    const auto arrived = seekVelocity(Vec3d{0.0, 0.0, 0.0}, Vec3d{3.0, 0.0, 0.0}, Vec3d{0.0, 0.0, 0.0}, params, 1.0 / 60.0);
    CHECK(arrived.x == doctest::Approx(0.0));
    CHECK(arrived.y == doctest::Approx(0.0));
    CHECK(arrived.z == doctest::Approx(0.0));

    // Arriving with residual velocity decelerates smoothly instead of
    // snapping to zero in one tick.
    const auto decelerating = seekVelocity(Vec3d{0.0, 0.0, 0.0}, Vec3d{3.0, 0.0, 0.0}, Vec3d{2.0, 0.0, 0.0}, params, 1.0 / 60.0);
    CHECK(decelerating.x == doctest::Approx(2.0 - params.maxAcceleration / 60.0).epsilon(1.0e-3));

    CHECK(arrivalSpeed(6.0, params) == doctest::Approx(std::sqrt(20.0)).epsilon(1.0e-3));

    const auto accelerating = seekVelocity(Vec3d{0.0, 0.0, 0.0}, Vec3d{100.0, 0.0, 0.0}, Vec3d{0.0, 0.0, 0.0}, params, 1.0 / 60.0);
    CHECK(accelerating.x == doctest::Approx(params.maxAcceleration / 60.0).epsilon(1.0e-3));
}

TEST_CASE("AI steering: entity points toward target and accelerates")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto target = starforge::spatial::WorldPosition::fromMeters(100.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(target.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    SteeringParams params;
    params.maxSpeed = 20.0;
    params.maxAcceleration = 60.0;
    params.arrivalRadius = 5.0;

    steerEntity(ship, *target, 1.0 / 60.0, params);

    REQUIRE(ship.has<starforge::ecs::Velocity>());
    const auto velocity = ship.get<starforge::ecs::Velocity>();
    CHECK(velocity.linear.x == doctest::Approx(1.0));
    CHECK(velocity.linear.y == doctest::Approx(0.0));
    CHECK(velocity.linear.z == doctest::Approx(0.0));

    const Vec4f orientation = ship.get<starforge::ecs::Transform>().orientation;
    const glm::quat orientationQuat{orientation.w, orientation.x, orientation.y, orientation.z};
    const glm::vec3 forward = orientationQuat * glm::vec3{0.0f, 0.0f, -1.0f};
    CHECK(forward.x == doctest::Approx(1.0f));
    CHECK(forward.y == doctest::Approx(0.0f));
    CHECK(forward.z == doctest::Approx(0.0f));
}

TEST_CASE("AI steering: separation pushes nearby agents apart")
{
    SteeringParams params;
    params.maxSpeed = 20.0;
    params.separationRadius = 10.0;
    params.separationWeight = 1.0;

    const auto result = separationVelocity(Vec3d{0.0, 0.0, 0.0}, {Vec3d{2.0, 0.0, 0.0}}, params);
    CHECK(result.x == doctest::Approx(-12.8).epsilon(1.0e-3));
    CHECK(result.y == doctest::Approx(0.0));
    CHECK(result.z == doctest::Approx(0.0));

    // Exact overlap must not stick: it receives the deterministic maximum push.
    const auto overlap = separationVelocity(Vec3d{0.0, 0.0, 0.0}, {Vec3d{0.0, 0.0, 0.0}}, params);
    CHECK(overlap.x == doctest::Approx(20.0));
}

TEST_CASE("AI steering: antiparallel heading stays upright")
{
    const Vec4f orientation = headingForDirection(Vec3d{-100.0, 0.0, 0.0});
    const glm::quat orientationQuat{orientation.w, orientation.x, orientation.y, orientation.z};
    const glm::vec3 forward = orientationQuat * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 up = orientationQuat * glm::vec3{0.0f, 1.0f, 0.0f};
    CHECK(forward.x == doctest::Approx(-1.0f));
    CHECK(forward.y == doctest::Approx(0.0f));
    CHECK(forward.z == doctest::Approx(0.0f));
    CHECK(up.x == doctest::Approx(0.0f));
    CHECK(up.y == doctest::Approx(1.0f));
    CHECK(up.z == doctest::Approx(0.0f));
}
