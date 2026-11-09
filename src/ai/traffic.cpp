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

} // namespace

std::vector<RoutePoint> routeToStation([[maybe_unused]] const spatial::WorldPosition& from,
                                        const spatial::WorldPosition& station,
                                        std::uint8_t dockSlot)
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

bool advanceRoute([[maybe_unused]] ecs::Ecs& world, flecs::entity entity, double dt,
                  const ecs::SpatialIndex& index, TrafficManager* traffic)
{
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

    // Packet-separation neighbours, stack buffer (no per-tick allocation),
    // via the shared helper (excludes stations so the dock glide works).
    std::array<math::Vec3d, 32> neighbours {};
    const std::size_t neighbourCount = [&] {
        // transform is read below; fetch it here for the neighbourhood.
        return collectNeighbours(entity, index.findNeighborhood(entity.get<ecs::Transform>().position),
                                 entity.get<ecs::Transform>(), neighbours);
    }();

    const auto completeRoute = [&] {
        // Copy the station handle before the structural removal below:
        // remove<> can move the entity to a new archetype and invalidate
        // the `route`/`agent` references.
        const flecs::entity station = route.station;
        route.complete = true;
        agent.state = BehaviorState::Stationary;
        // Drop the destination so the FSM cannot bounce
        // Stationary -> Docking -> Transit forever on a stale
        // NavigationTarget (the ship is already parked at the dock).
        entity.remove<NavigationTarget>();
        entity.modified<AiAgent>();
        if (traffic != nullptr && station.is_alive())
        {
            traffic->releaseDockSlot(station, entity);
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
            steerEntity(entity, point.position, dt, steeringParamsFor(agent, point.arrivalRadius),
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
    // Pass-through waypoints brake later (half arrival radius) so the
    // ship does not decelerate to a stop at every waypoint; the
    // advancement check still uses the full arrivalRadius above.
    const double steerArrival =
        route.currentPoint + 1 >= route.pointCount ? activePoint.arrivalRadius : activePoint.arrivalRadius * 0.5;
    steerEntity(entity, activePoint.position, dt, steeringParamsFor(agent, steerArrival), neighbourSpan);

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
