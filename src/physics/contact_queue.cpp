#include "contact_queue.hpp"

#include "ecs/components.hpp"

namespace starforge::physics
{

void ContactQueue::push(ContactEventData event)
{
    std::lock_guard lock(mutex_);
    events_.push_back(event);
}

std::size_t ContactQueue::size() const
{
    std::lock_guard lock(mutex_);
    return events_.size();
}

void ContactQueue::clearTransientEvents(ecs::Ecs& world)
{
    // The query is scoped to this call: cached queries would outlive
    // their Flecs world if the Ecs wrapper dies first (Flecs asserts on
    // query destruction against a dead world). Construction cost is
    // negligible next to the flush itself.
    auto query = world.world().query<ecs::CollisionData>();
    // Collect first: structural changes during iteration are deferred in
    // Flecs but simpler to reason about in two passes.
    std::vector<flecs::entity> targets;
    query.each([&targets](flecs::entity entity, ecs::CollisionData&) {
        targets.push_back(entity);
    });
    for (const auto target : targets)
    {
        target.remove<ecs::CollisionEvent>();
        target.remove<ecs::CollisionData>();
    }
}

void ContactQueue::flush(ecs::Ecs& world, const spatial::WorldPosition& camera)
{
    clearTransientEvents(world);

    std::vector<ContactEventData> pending;
    {
        std::lock_guard lock(mutex_);
        pending.swap(events_);
    }

    auto* flecsWorld = world.world().c_ptr();

    for (const auto& event : pending)
    {
        const math::Vec3d delta{static_cast<double>(event.islandPoint.x), static_cast<double>(event.islandPoint.y),
                                  static_cast<double>(event.islandPoint.z)};
        auto rebased = camera.translated(delta);
        if (!rebased.has_value())
        {
            continue;
        }

        const auto emitOn =
            [flecsWorld, &rebased](std::uint64_t targetId, std::uint64_t otherId, const math::Vec3f& normal,
                                   float impulse) {
                if (targetId == 0)
                {
                    return;
                }
                const flecs::entity target(flecsWorld, static_cast<flecs::entity_t>(targetId));
                if (!target.is_valid() || !target.is_alive())
                {
                    return;
                }
                // Re-bind the other handle to the world so gameplay code
                // can query it safely.
                const flecs::entity other =
                    otherId != 0 ? flecs::entity(flecsWorld, static_cast<flecs::entity_t>(otherId))
                                 : flecs::entity::null();
                target.add<ecs::CollisionEvent>();
                // Append to the contact buffer (fixed capacity; events
                // beyond capacity are dropped).
                ecs::CollisionData data{};
                if (target.has<ecs::CollisionData>())
                {
                    data = target.get<ecs::CollisionData>();
                }
                if (data.count < ecs::CollisionData::kMaxContacts)
                {
                    data.contacts[data.count++] = {.other = other, .point = *rebased, .normal = normal, .impulse = impulse};
                }
                target.set<ecs::CollisionData>(data);
            };

        // Equal-and-opposite normals (Newton's third law).
        emitOn(event.idA, event.idB, event.normal, event.impulse);
        emitOn(event.idB, event.idA, math::Vec3f{-event.normal.x, -event.normal.y, -event.normal.z}, event.impulse);
    }
}

} // namespace starforge::physics
