#include "world.hpp"

#include <glm/gtc/quaternion.hpp>

#include <utility>

namespace starforge::ecs
{

Ecs::Ecs()
{
    world_.component<Transform>("Transform");
    world_.component<Velocity>("Velocity");
    world_.component<ShipTag>("ShipTag");
    world_.component<AsteroidTag>("AsteroidTag");
    world_.component<StationTag>("StationTag");
    world_.component<ProjectileTag>("ProjectileTag");

    // Cached once: only entities with both Transform and Velocity move.
    movementQuery_ = world_.query<Transform, const Velocity>();
    asteroidQuery_ = world_.query<const Transform, const AsteroidTag>();
}

Ecs::Ecs(Ecs&& other) noexcept
    : world_(std::move(other.world_)), movementQuery_(std::move(other.movementQuery_)),
      asteroidQuery_(std::move(other.asteroidQuery_))
{
}

flecs::entity Ecs::createShip(const spatial::WorldPosition& pos, const math::Vec3d& vel)
{
    auto entity = world_.entity().set<Transform>({pos}).add<ShipTag>();
    if (vel.x != 0.0 || vel.y != 0.0 || vel.z != 0.0)
    {
        entity.set<Velocity>({vel});
    }
    return entity;
}

flecs::entity Ecs::createAsteroid(const spatial::WorldPosition& pos, const math::Vec3d& vel)
{
    auto entity = world_.entity().set<Transform>({pos}).add<AsteroidTag>();
    if (vel.x != 0.0 || vel.y != 0.0 || vel.z != 0.0)
    {
        entity.set<Velocity>({vel});
    }
    return entity;
}

flecs::entity Ecs::createStation(const spatial::WorldPosition& pos)
{
    // Static: no Velocity component.
    return world_.entity().set<Transform>({pos}).add<StationTag>();
}

flecs::entity Ecs::createProjectile(const spatial::WorldPosition& pos, const math::Vec3d& vel)
{
    return world_.entity().set<Transform>({pos}).set<Velocity>({vel}).add<ProjectileTag>();
}

// Fixed-step movement: linear translation in double precision (precision
// preserved at any range) plus quaternion orientation integration.
// Deterministic for a fixed dt sequence; no wall-clock involvement.
void Ecs::step(float dt)
{
    if (dt == 0.0f)
    {
        return;
    }
    movementQuery_.each([dt](flecs::entity, Transform& transform, const Velocity& velocity) {
        const math::Vec3d displacement = velocity.linear * static_cast<double>(dt);
        if (auto moved = transform.position.translated(displacement); moved.has_value())
        {
            transform.position = *moved;
        }

        const float rate = glm::length(velocity.angular);
        if (rate > 0.0f)
        {
            const glm::vec3 axis = glm::normalize(velocity.angular);
            const glm::quat delta = glm::angleAxis(rate * dt, axis);
            glm::quat orientation{transform.orientation.w, transform.orientation.x, transform.orientation.y,
                                  transform.orientation.z};
            orientation = glm::normalize(delta * orientation);
            transform.orientation = math::Vec4f{orientation.x, orientation.y, orientation.z, orientation.w};
        }
    });
}

std::uint64_t Ecs::entityCount() const
{
    return static_cast<std::uint64_t>(world_.count<Transform>());
}

} // namespace starforge::ecs
