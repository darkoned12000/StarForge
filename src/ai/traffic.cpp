#include "ai/traffic.hpp"

#include "ai/steering.hpp"
#include "spatial/camera_relative.hpp"

#include <algorithm>

namespace starforge::ai
{

namespace
{

[[nodiscard]] std::uint64_t entityKey(flecs::entity entity) noexcept
{
    return entity.id();
}

[[nodiscard]] SteeringParams routeSteeringParams(const AiAgent& agent, double arrivalRadius) noexcept
{
    SteeringParams params;
    params.maxSpeed = agent.maxSpeed;
    params.maxAcceleration = agent.maxAcceleration;
    params.arrivalRadius = arrivalRadius;
    params.separationRadius = 25.0;
    params.separationWeight = 1.0;
    return params;
}

[[nodiscard]] std::vector<math::Vec3d> nearbyPositions(flecs::entity entity, const ecs::SpatialIndex& index,
                                                        const ecs::Transform& self)
{
    std::vector<math::Vec3d> positions;
    positions.reserve(8);
    for (const auto other : index.findNeighborhood(self.position))
    {
        if (other == entity || !other.is_alive() || !other.has<ecs::Transform>() ||
            other.has<ecs::StationTag>())
        {
            continue;
        }
        const math::Vec3d relative =
            spatial::toCameraRelative(other.get<ecs::Transform>().position, self.position);
        if (glm::length(relative) <= 100.0)
        {
            positions.push_back(relative);
        }
    }
    return positions;
}

} // namespace

std::vector<RoutePoint> routeToStation([[maybe_unused]] const spatial::WorldPosition& from,
                                        const spatial::WorldPosition& station,
                                        std::uint8_t dockSlot) noexcept
{
    const double lateral = static_cast<double>(dockSlot) * 25.0;
    const auto approach = station + math::Vec3d{lateral, 0.0, 250.0};
    const auto dockMarker = station + math::Vec3d{lateral, 0.0, 50.0};
    const auto dock = station + math::Vec3d{lateral, 0.0, 5.0};
    return {
        {approach, 20.0f},
        {dockMarker, 10.0f},
        {dock, 2.0f},
    };
}

void assignRoute(flecs::entity entity, const spatial::WorldPosition& from,
                 const spatial::WorldPosition& station, std::uint8_t dockSlot)
{
    const auto points = routeToStation(from, station, dockSlot);
    NavigationRoute route;
    route.points = points;
    NavigationTarget navigation;
    navigation.positionHint = points.front().position;
    navigation.arrivalRadius = points.front().arrivalRadius;
    entity.set<NavigationRoute>(std::move(route));
    entity.set<NavigationTarget>(navigation);
}

bool advanceRoute(ecs::Ecs& world, flecs::entity entity, double dt,
                  const ecs::SpatialIndex& index)
{
    (void)world;
    if (!entity.has<NavigationRoute>() || !entity.has<AiAgent>() || !entity.has<ecs::Transform>())
    {
        return true;
    }

    auto& route = entity.get_mut<NavigationRoute>();
    auto& agent = entity.get_mut<AiAgent>();
    if (route.complete)
    {
        return true;
    }
    if (route.points.empty())
    {
        route.complete = true;
        return true;
    }

    const auto& transform = entity.get<ecs::Transform>();
    const RoutePoint& point = route.points[route.currentPoint];
    const math::Vec3d currentToPoint = spatial::toCameraRelative(point.position, transform.position);
    const double distance = glm::length(currentToPoint);

    if (route.currentPoint + 1 >= route.points.size() && distance <= point.arrivalRadius)
    {
        route.dwellTimer += dt;
        if (route.dwellTimer < route.dwellSeconds)
        {
            steerEntity(entity, point.position, dt, routeSteeringParams(agent, point.arrivalRadius),
                        nearbyPositions(entity, index, transform));
            return false;
        }
        route.complete = true;
        agent.state = BehaviorState::Stationary;
        if (entity.has<ecs::Velocity>())
        {
            entity.set<ecs::Velocity>({{0.0, 0.0, 0.0}, {0.0f, 0.0f, 0.0f}});
        }
        return true;
    }

    if (distance <= point.arrivalRadius)
    {
        route.currentPoint += 1;
        route.dwellTimer = 0.0;
        if (route.currentPoint >= route.points.size())
        {
            route.complete = true;
            agent.state = BehaviorState::Stationary;
            if (entity.has<ecs::Velocity>())
            {
                entity.set<ecs::Velocity>({{0.0, 0.0, 0.0}, {0.0f, 0.0f, 0.0f}});
            }
            return true;
        }
    }

    const RoutePoint& activePoint = route.points[route.currentPoint];
    steerEntity(entity, activePoint.position, dt, routeSteeringParams(agent, activePoint.arrivalRadius),
                nearbyPositions(entity, index, transform));

    if (entity.has<NavigationTarget>())
    {
        auto& navigation = entity.get_mut<NavigationTarget>();
        navigation.positionHint = activePoint.position;
        navigation.arrivalRadius = activePoint.arrivalRadius;
        entity.modified<NavigationTarget>();
    }
    return false;
}

void TrafficManager::clear() noexcept
{
    slots_.clear();
}

std::optional<std::uint8_t> TrafficManager::reserveDockSlot(flecs::entity station, flecs::entity ship,
                                                              std::uint8_t slotCount)
{
    if (slotCount == 0)
    {
        slotCount = 1;
    }
    auto& slots = slots_[entityKey(station)];
    if (slots.size() < slotCount)
    {
        slots.resize(slotCount);
    }
    for (std::uint8_t slot = 0; slot < slotCount; ++slot)
    {
        if (!slots[slot].has_value() || !slots[slot]->is_alive())
        {
            slots[slot] = ship;
            return slot;
        }
    }
    return std::nullopt;
}

void TrafficManager::releaseDockSlot(flecs::entity station, flecs::entity ship) noexcept
{
    const auto found = slots_.find(entityKey(station));
    if (found == slots_.end())
    {
        return;
    }
    for (auto& occupant : found->second)
    {
        if (occupant.has_value() && occupant.value() == ship)
        {
            occupant.reset();
            break;
        }
    }
}

} // namespace starforge::ai
