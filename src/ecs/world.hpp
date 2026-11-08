#pragma once

#include "components.hpp"

#include <flecs.h>

#include <cstdint>

namespace starforge::ecs
{

// ECS world: owns the Flecs world and registers components. Movement is a
// deterministic fixed-step query over (Transform, Velocity), so only
// moving entities pay the integration cost and results depend solely on
// the dt sequence (not wall-clock time).
class Ecs
{
public:
    Ecs();
    ~Ecs() = default;

    Ecs(Ecs&& other) noexcept;
    Ecs& operator=(Ecs&& other) noexcept;
    Ecs(const Ecs&) = delete;
    Ecs& operator=(const Ecs&) = delete;

    [[nodiscard]] flecs::entity createShip(const spatial::WorldPosition& pos, const math::Vec3d& vel = {});
    [[nodiscard]] flecs::entity createAsteroid(const spatial::WorldPosition& pos, const math::Vec3d& vel = {});
    [[nodiscard]] flecs::entity createStation(const spatial::WorldPosition& pos);
    [[nodiscard]] flecs::entity createProjectile(const spatial::WorldPosition& pos, const math::Vec3d& vel);

    // Advances movement by dt seconds (fixed-step, deterministic).
    void step(float dt);

    [[nodiscard]] flecs::world& world() noexcept { return world_; }
    [[nodiscard]] const flecs::world& world() const noexcept { return world_; }

    [[nodiscard]] std::uint64_t entityCount() const;

private:
    flecs::world world_;
    flecs::query<Transform, const Velocity> movementQuery_;
};

} // namespace starforge::ecs
