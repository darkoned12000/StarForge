#include "game_bridge.hpp"

#include "ecs/components.hpp"
#include "ecs/world.hpp"
#include "math/math.hpp"
#include "spatial/world_position.hpp"

#include <utility>

namespace starforge::game
{

struct GameWorld::Impl
{
    ecs::Ecs ecs;
    spatial::WorldPosition start;
    flecs::entity ship;

    Impl()
        : start(spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0).value()),
          ship(ecs.createShip(start, {0.0, 0.0, 0.0}))
    {
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

} // namespace starforge::game
