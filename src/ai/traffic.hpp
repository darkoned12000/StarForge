#pragma once

#include "ai/components.hpp"
#include "ecs/spatial_index.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace starforge::ai
{

[[nodiscard]] std::vector<RoutePoint> routeToStation(const spatial::WorldPosition& from,
                                                       const spatial::WorldPosition& station,
                                                       std::uint8_t dockSlot = 0);

// Assign a route and the first navigation target to the entity. The points
// are deterministic: approach on a lateral slot offset, then final dock.
// stationEntity is the station the route targets (for dock-slot release);
// it defaults to none for position-only routes.
void assignRoute(flecs::entity entity, const spatial::WorldPosition& from,
                 const spatial::WorldPosition& station, std::uint8_t dockSlot = 0,
                 flecs::entity stationEntity = {});

class TrafficManager;

[[nodiscard]] bool advanceRoute(ecs::Ecs& world, flecs::entity entity, double dt,
                                const ecs::SpatialIndex& index, TrafficManager* traffic = nullptr);
// Round-robin station dock slots. Entity handles are allocated in creation
// order, so calling reserve in id order is deterministic for a fixed spawn
// sequence.
class TrafficManager
{
public:
    void clear() noexcept;

    [[nodiscard]] std::optional<std::uint8_t> reserveDockSlot(flecs::entity station, flecs::entity ship,
                                                              std::uint8_t slotCount = 16);
    void releaseDockSlot(flecs::entity station, flecs::entity ship) noexcept;

private:
    std::unordered_map<uint64_t, std::vector<std::optional<flecs::entity>>> slots_;
};

} // namespace starforge::ai
