#include "bridge.hpp"

#include "audio/spatial.hpp"
#include "spatial/camera_relative.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <utility>

namespace starforge::audio
{
namespace
{

// Forward/up for a listener with the given orientation (xyzw), matching
// starforge::math conventions: forward is -Z, up is +Y.
void listenerAxes(const ecs::Transform& transform, math::Vec3f& forward, math::Vec3f& up)
{
    const glm::quat orientation{transform.orientation.w, transform.orientation.x, transform.orientation.y,
                                transform.orientation.z};
    const glm::vec3 fwd = orientation * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 u = orientation * glm::vec3{0.0f, 1.0f, 0.0f};
    forward = {fwd.x, fwd.y, fwd.z};
    up = {u.x, u.y, u.z};
}

// Velocity from the optional Velocity component (double -> float32).
[[nodiscard]] math::Vec3f velocityOf(const flecs::entity entity)
{
    if (entity.has<ecs::Velocity>())
    {
        const auto velocity = entity.get<ecs::Velocity>();
        return cameraRelativeVelocity(velocity.linear);
    }
    return {};
}

} // namespace

void registerAudioComponents(ecs::Ecs& world)
{
    world.world().component<AudioEmitter>("AudioEmitter");
    world.world().component<AudioListener>("AudioListener");
}

void attachEmitter(ecs::Ecs& world, flecs::entity entity, const AudioEmitter& emitter, SoundBank& bank)
{
    (void)world;
    entity.set<AudioEmitter>(emitter);
    (void)bank.setEmitterAttenuation(emitter.sound, emitter.minDistance, emitter.maxDistance, emitter.rolloff);
}

void syncAudio(ecs::Ecs& world, AudioContext& context, SoundBank& bank, const spatial::WorldPosition& camera)
{
    // Listener: every AudioListener entity with a Transform drives
    // listener 0 (single-player design; last one wins deterministically
    // in query order).
    world.world().query<const ecs::Transform, const AudioListener>().each(
        [&context, &camera](flecs::entity entity, const ecs::Transform& transform, const AudioListener&) {
            math::Vec3f forward;
            math::Vec3f up;
            listenerAxes(transform, forward, up);
            (void)context.setListenerPose(toCameraRelativeFloat(transform.position, camera),
                                          velocityOf(entity), forward, up);
        });

    // Emitters: per-entity camera-relative pose + velocity.
    world.world().query<const ecs::Transform, const AudioEmitter>().each(
        [&bank, &camera](flecs::entity entity, const ecs::Transform& transform, const AudioEmitter& emitter) {
            if (!emitter.sound.isValid())
            {
                return;
            }
            (void)bank.setEmitterPose(emitter.sound, toCameraRelativeFloat(transform.position, camera),
                                      velocityOf(entity), emitter.dopplerFactor);
        });
}

void TriggerQueue::push(TriggerEvent event)
{
    std::lock_guard lock(mutex_);
    events_.push_back(event);
}

std::size_t TriggerQueue::size() const
{
    std::lock_guard lock(mutex_);
    return events_.size();
}

void TriggerQueue::flush(ecs::Ecs& world, SoundBank& bank)
{
    std::vector<TriggerEvent> pending;
    {
        std::lock_guard lock(mutex_);
        pending.swap(events_);
    }
    auto* flecsWorld = world.world().c_ptr();
    for (const auto& event : pending)
    {
        if (event.entityId == 0)
        {
            continue;
        }
        const flecs::entity target(flecsWorld, static_cast<flecs::entity_t>(event.entityId));
        if (!target.is_valid() || !target.is_alive())
        {
            continue;
        }
        if (!target.has<AudioEmitter>())
        {
            continue;
        }
        const auto emitter = target.get<AudioEmitter>();
        if (!emitter.sound.isValid())
        {
            continue;
        }
        // Triggered volume modulates the configured base volume and
        // re-cuts if still playing (second impact within one clip).
        const float triggeredVolume = bank.volume(emitter.sound) * event.volume;
        (void)bank.playWithVolume(emitter.sound, triggeredVolume, true);
    }
}

void pushCollisionTriggers(ecs::Ecs& world, TriggerQueue& queue)
{
    constexpr float kMinImpulse = 1.0f;
    constexpr float kMaxImpulse = 100.0f;
    world.world().query<ecs::CollisionEvent>().each(
        [&world, &queue](flecs::entity entity, const ecs::CollisionEvent&) {
            float maxImpulse = 0.0f;
            if (entity.has<ecs::CollisionData>())
            {
                const auto data = entity.get<ecs::CollisionData>();
                for (std::uint32_t i = 0; i < data.count; ++i)
                {
                    maxImpulse = std::max(maxImpulse, data.contacts[i].impulse);
                }
            }
            // Sub-threshold / degenerate contacts are silent by design:
            // a feather touch must not thump at maximum loudness.
            if (maxImpulse < kMinImpulse)
            {
                return;
            }
            TriggerEvent event;
            event.entityId = entity.id();
            event.volume = std::clamp(maxImpulse / kMaxImpulse, 0.05f, 1.0f);
            queue.push(event);
        });
}

} // namespace starforge::audio
