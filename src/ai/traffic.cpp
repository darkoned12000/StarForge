#include "ai/traffic.hpp"

#include "ai/steering.hpp"
#include "spatial/camera_relative.hpp"

#include <algorithm>
#include <array>
#include <span>

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

[[nodiscard]] std::size_t nearbyPositions(flecs::entity entity, const ecs::SpatialIndex& index,
                                           const ecs::Transform& self,
                                           std::array<math::Vec3d, 32>& out)
{
    std::size_t count = 0;
    for (const auto other : index.findNeighborhood(self.position))
    {
        if (other == entity || !other.is_alive() || !other.has<ecs::Transform>() ||
            other.has<ecs::StationTag>())
        {
            continue;
        }
        const math::Vec3d relative =
            spatial::toCameraRelative(other.get<ecs::Transform>().position, self.position);
        if (glm::length(relative) <= 100.0 && count < out.size())
        {
            out[count++] = relative;
        }
    }
    return count;
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
                 const spatial::WorldPosition& station, std::uint8_t dockSlot,
                 flecs::entity stationEntity)
{
    const auto points = routeToStation(from, station, dockSlot);
    NavigationRoute route;
    route.pointCount = std::min(points.size(), NavigationRoute::kMaxRoutePoints);
    for (std::size_t i = 0; i < route.pointCount; ++i)
    {
        route.points[i] = points[i];
    }
    route.station = stationEntity;
    // Route assignment is transit intent: idle ships start moving now so
    // advanceRoute's Transit/Docking gate admits them. Combat states are
    // left untouched — the FSM returns to Transit when the fight ends
    // because NavigationTarget still has a destination.
    if (entity.has<AiAgent>() && entity.get<AiAgent>().state == BehaviorState::Patrol)
    {
        auto& agent = entity.get_mut<AiAgent>();
        agent.state = BehaviorState::Transit;
        entity.modified<AiAgent>();
    }
    NavigationTarget navigation;
    navigation.positionHint = points.front().position;
    navigation.arrivalRadius = points.front().arrivalRadius;
    entity.set<NavigationRoute>(std::move(route));
    entity.set<NavigationTarget>(navigation);
}

bool advanceRoute(ecs::Ecs& world, flecs::entity entity, double dt,
                  const ecs::SpatialIndex& index, TrafficManager* traffic)
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

    // Combat owns the controls: only civilian states follow the route.
    // Without this gate, route steering overwrote Engaging/Evading
    // velocity every tick and ships flew through combat on rails.
    if (agent.state != BehaviorState::Transit && agent.state != BehaviorState::Docking)
    {
        return false;
    }

    // Packet-separation neighbours, stack buffer (no per-tick allocation).
    std::array<math::Vec3d, 32> neighbours {};
    const std::size_t neighbourCount = [&] {
        // transform is read below; fetch it here for the neighbourhood.
        return nearbyPositions(entity, index, entity.get<ecs::Transform>(), neighbours);
    }();

    const auto completeRoute = [&] {
        route.complete = true;
        agent.state = BehaviorState::Stationary;
        // Drop the destination so the FSM cannot bounce
        // Stationary -> Docking -> Transit forever on a stale
        // NavigationTarget (the ship is already parked at the dock).
        entity.remove<NavigationTarget>();
        if (traffic != nullptr && route.station.is_alive())
        {
            traffic->releaseDockSlot(route.station, entity);
        }
        if (entity.has<ecs::Velocity>())
        {
            entity.set<ecs::Velocity>({{0.0, 0.0, 0.0}, {0.0f, 0.0f, 0.0f}});
        }
    };

    if (route.empty())
    {
        completeRoute();
        return true;
    }

    const auto& transform = entity.get<ecs::Transform>();
    const RoutePoint& point = route.points[route.currentPoint];
    const math::Vec3d currentToPoint = spatial::toCameraRelative(point.position, transform.position);
    const double distance = glm::length(currentToPoint);
    const std::span<const math::Vec3d> neighbourSpan(neighbours.data(), neighbourCount);

    if (route.currentPoint + 1 >= route.pointCount && distance <= point.arrivalRadius)
    {
        route.dwellTimer += dt;
        if (route.dwellTimer < route.dwellSeconds)
        {
            steerEntity(entity, point.position, dt, routeSteeringParams(agent, point.arrivalRadius),
                        neighbourSpan);
            return false;
        }
        completeRoute();
        return true;
    }

    if (distance <= point.arrivalRadius)
    {
        route.currentPoint += 1;
        route.dwellTimer = 0.0;
        if (route.currentPoint >= route.pointCount)
        {
            completeRoute();
            return true;
        }
    }

    const RoutePoint& activePoint = route.points[route.currentPoint];
    steerEntity(entity, activePoint.position, dt, routeSteeringParams(agent, activePoint.arrivalRadius),
                neighbourSpan);

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
