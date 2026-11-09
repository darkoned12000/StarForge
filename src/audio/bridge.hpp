#pragma once

// ECS <-> audio bridge (M12.4): moves entities between ECS Transform in
// world space (WorldPosition) and miniaudio emitters/listeners in
// camera-relative float32 — the same single-cast rule as rendering
// (M3) and physics (M11). miniaudio itself is never exposed.
//
// Threading: the audio (mixing) thread never touches Flecs. Discrete
// one-shot triggers (e.g. collision impact thumps) flow through
// TriggerQueue: any thread pushes events; the main thread flushes the
// queue into SoundBank::play after the ECS step, mirroring M11's
// ContactQueue pattern.

#include "audio_context.hpp"
#include "ecs/world.hpp"
#include "sound.hpp"
#include "spatial/world_position.hpp"

#include <flecs.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace starforge::audio
{

// A spatial voice attached to an entity. `sound` is a SoundBank handle
// loaded by the caller (see attachEmitter). Distance parameters map to
// the miniaudio inverse-distance attenuation model: maxDistance clamps
// the model to a positive floor gain (it does not silence the voice;
// silence is a game-layer decision).
struct AudioEmitter
{
    SoundHandle sound;
    float rolloff = 1.0f;        // Attenuation aggressiveness (>= 0.1).
    float minDistance = 1.0f;    // Full gain within this radius (m).
    float maxDistance = 1000.0f; // Gain floor past this range (m).
    float dopplerFactor = 1.0f;  // Doppler strength (0 disables).
};

// Marks the listener entity (typically the ship). Its Transform and
// Velocity drive AudioContext::setListenerPose every sync.
struct AudioListener
{
};

// Registers AudioEmitter/AudioListener component IDs with the world.
// Call once per Ecs world before attaching audio components.
void registerAudioComponents(ecs::Ecs& world);

// Registers removal cleanup for AudioEmitter: destroying or removing the
// component stops its voice so looped emitters don't become ghosts.
// The bank must outlive the world.
void registerAudioEmitterCleanup(ecs::Ecs& world, SoundBank& bank);

// Attaches an AudioEmitter component for a voice that must already be
// loaded in the bank, and forwards its attenuation parameters
// (minDistance/maxDistance/rolloff) to the voice.
void attachEmitter(ecs::Ecs& world, flecs::entity entity, const AudioEmitter& emitter, SoundBank& bank);

// Syncs all listener and emitter poses for one frame: reads ECS
// Transforms/Velocities, converts camera-relative, and forwards them
// to the engine listener and the emitter voices. Entities without a
// Velocity component are treated as stationary.
void syncAudio(ecs::Ecs& world, AudioContext& context, SoundBank& bank, const spatial::WorldPosition& camera);

// One discrete trigger: play the AudioEmitter's sound on the entity.
struct TriggerEvent
{
    std::uint64_t entityId = 0;
    float volume = 1.0f; // Impulse-derived one-shot gain in [0, 1].
};

// Thread-safe one-shot trigger queue. Producer callbacks (physics
// workers, audio-adjacent threads) only push; the main thread flushes
// after the ECS step. The flush does not mutate Flecs — it only calls
// SoundBank::play — and dead entity IDs are skipped.
class TriggerQueue
{
public:
    void push(TriggerEvent event);
    [[nodiscard]] std::size_t size() const;
    void flush(ecs::Ecs& world, SoundBank& bank);

private:
    mutable std::mutex mutex_;
    std::vector<TriggerEvent> events_;
};

// Scans entities carrying CollisionEvent + CollisionData and pushes one
// trigger per collided entity whose impulse meets the minimum audible
// threshold (sub-threshold contacts are ignored). Call on the main
// thread after ContactQueue::flush, e.g. to feed the TriggerQueue.
void pushCollisionTriggers(ecs::Ecs& world, TriggerQueue& queue);

} // namespace starforge::audio
