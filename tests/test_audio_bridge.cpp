// Audio ECS bridge tests (M12.4): component registration, per-frame
// pose sync (camera-relative), and the deferred trigger queue.
// The physics (Jolt -> ECS CollisionEvent) hop is proven by M11; here
// the ECS -> audio hop is proven from CollisionEvent components.
#include <doctest/doctest.h>

#include "audio/audio_context.hpp"
#include "audio/bridge.hpp"
#include "audio/sound.hpp"
#include "audio/tone_generator.hpp"
#include "ecs/world.hpp"

#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace starforge::audio;

namespace
{

std::expected<AudioContext, std::string> createNullContext()
{
    AudioContext::Config config;
    config.backend = AudioContext::Config::Backend::Null;
    return AudioContext::create(config);
}

} // namespace

TEST_CASE("Bridge: syncAudio tracks listener and emitter poses")
{
    using namespace starforge::ecs;
    using namespace starforge::spatial;

    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    Ecs world;
    registerAudioComponents(world);
    const auto camera = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camera.has_value());

    // Listener at the camera origin, moving at 10 m/s in +X; emitter
    // stationary at (10, 0, 0) metres.
    const auto shipPos = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto emitterPos = WorldPosition::fromMeters(10.0, 0.0, 0.0);
    REQUIRE(shipPos.has_value());
    REQUIRE(emitterPos.has_value());
    const auto ship = world.createShip(*shipPos, {10.0, 0.0, 0.0});
    ship.add<AudioListener>();
    const auto rock = world.createAsteroid(*emitterPos, {});
    auto emitterHandle = bank.loadFromMemory(generateToneWav({}), {});
    REQUIRE(emitterHandle.has_value());
    attachEmitter(world, rock, {.sound = *emitterHandle}, bank);

    syncAudio(world, *context, bank, *camera);

    const auto listenerPos = context->listenerPosition();
    CHECK(listenerPos.x == doctest::Approx(0.0f));
    CHECK(context->listenerVelocity().x == doctest::Approx(10.0f));
    CHECK(context->listenerDirection().z == doctest::Approx(-1.0f));

    const auto emitter = bank.emitterPosition(*emitterHandle);
    CHECK(emitter.x == doctest::Approx(10.0f));
    CHECK(emitter.y == doctest::Approx(0.0f));
    CHECK(emitter.z == doctest::Approx(0.0f));
    const auto emitterVel = bank.emitterVelocity(*emitterHandle);
    CHECK(emitterVel.x == doctest::Approx(0.0f));
}

TEST_CASE("Bridge: emitter velocity follows its ECS Velocity")
{
    using namespace starforge::ecs;
    using namespace starforge::spatial;

    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    Ecs world;
    registerAudioComponents(world);
    const auto camera = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto rockPos = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camera.has_value());
    REQUIRE(rockPos.has_value());
    const auto rock = world.createAsteroid(*rockPos, {5.0, -3.0, 2.0});
    auto handle = bank.loadFromMemory(generateToneWav({}), {});
    REQUIRE(handle.has_value());
    attachEmitter(world, rock, {.sound = *handle}, bank);

    syncAudio(world, *context, bank, *camera);
    const auto vel = bank.emitterVelocity(*handle);
    CHECK(vel.x == doctest::Approx(5.0f));
    CHECK(vel.y == doctest::Approx(-3.0f));
    CHECK(vel.z == doctest::Approx(2.0f));
}

TEST_CASE("Bridge: collision trigger plays the entity's sound")
{
    using namespace starforge::ecs;
    using namespace starforge::spatial;

    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    Ecs world;
    registerAudioComponents(world);
    const auto camera = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto pos = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(camera.has_value());
    REQUIRE(pos.has_value());
    const auto ship = world.createShip(*pos, {});
    // Impact thump: one-shot, not started yet.
    auto handle = bank.loadFromMemory(generateToneWav({}), {});
    REQUIRE(handle.has_value());
    attachEmitter(world, ship, {.sound = *handle}, bank);
    CHECK(!bank.isPlaying(*handle));

    // Simulate the main-thread CollisionEvent hop (M11 ContactQueue
    // flush emits this state). Impulse 50 -> thump volume 0.5.
    ship.add<CollisionEvent>();
    CollisionData data{};
    data.count = 1;
    data.contacts[0].impulse = 50.0f;
    ship.set<CollisionData>(data);

    TriggerQueue queue;
    pushCollisionTriggers(world, queue);
    CHECK(queue.size() == 1U);
    queue.flush(world, bank);
    CHECK(bank.isPlaying(*handle));
    CHECK(queue.size() == 0U);
    REQUIRE(bank.stop(*handle));
}

TEST_CASE("Bridge: sub-threshold impulse is silent (feather touch)")
{
    using namespace starforge::ecs;
    using namespace starforge::spatial;

    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    Ecs world;
    registerAudioComponents(world);
    const auto pos = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(pos.has_value());
    const auto ship = world.createShip(*pos, {});
    auto handle = bank.loadFromMemory(generateToneWav({}), {});
    REQUIRE(handle.has_value());
    attachEmitter(world, ship, {.sound = *handle}, bank);

    ship.add<CollisionEvent>();
    CollisionData data{};
    data.count = 1;
    data.contacts[0].impulse = 0.2f; // Glancing touch.
    ship.set<CollisionData>(data);

    TriggerQueue queue;
    pushCollisionTriggers(world, queue);
    CHECK(queue.size() == 0U); // Ignored: no full-volume thump.
    queue.flush(world, bank);
    CHECK(!bank.isPlaying(*handle));
}

TEST_CASE("Bridge: trigger volume modulates base volume without clobbering")
{
    using namespace starforge::ecs;
    using namespace starforge::spatial;

    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    Ecs world;
    registerAudioComponents(world);
    const auto pos = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(pos.has_value());
    const auto ship = world.createShip(*pos, {});
    // Quiet emitter: base volume 0.4.
    SoundConfig config;
    config.volume = 0.4f;
    auto handle = bank.loadFromMemory(generateToneWav({}), config);
    REQUIRE(handle.has_value());
    attachEmitter(world, ship, {.sound = *handle}, bank);
    CHECK(bank.volume(*handle) == doctest::Approx(0.4f));

    TriggerQueue queue;
    queue.push({.entityId = ship.id(), .volume = 1.0f});
    queue.flush(world, bank);
    CHECK(bank.isPlaying(*handle));
    // Base volume is preserved for the next playback (no clobber).
    CHECK(bank.volume(*handle) == doctest::Approx(0.4f));
    REQUIRE(bank.stop(*handle));
    // A louder re-trigger still does not raise the configured base.
    queue.push({.entityId = ship.id(), .volume = 1.0f});
    queue.flush(world, bank);
    CHECK(bank.volume(*handle) == doctest::Approx(0.4f));
    REQUIRE(bank.stop(*handle));
}

TEST_CASE("Bridge: attachEmitter applies attenuation parameters")
{
    using namespace starforge::ecs;
    using namespace starforge::spatial;

    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    Ecs world;
    registerAudioComponents(world);
    const auto pos = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(pos.has_value());
    const auto rock = world.createAsteroid(*pos, {});
    auto handle = bank.loadFromMemory(generateToneWav({}), {});
    REQUIRE(handle.has_value());
    CHECK(bank.setEmitterAttenuation(*handle, 2.0f, 500.0f, 1.5f));
    // attachEmitter re-applies component values (explicit values win).
    attachEmitter(world, rock, {.sound = *handle, .rolloff = 1.5f, .minDistance = 2.0f, .maxDistance = 500.0f},
                  bank);
    // 2D buses reject attenuation.
    SoundConfig uiConfig;
    uiConfig.bus = Bus::UI;
    auto uiHandle = bank.loadFromMemory(generateToneWav({}), uiConfig);
    REQUIRE(uiHandle.has_value());
    CHECK(!bank.setEmitterAttenuation(*uiHandle, 1.0f, 100.0f, 1.0f));
}

TEST_CASE("Bridge: trigger queue is thread-safe")
{
    using namespace starforge::ecs;
    using namespace starforge::spatial;

    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    Ecs world;
    registerAudioComponents(world);
    const auto pos = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    REQUIRE(pos.has_value());
    const auto ship = world.createShip(*pos, {});
    auto handle = bank.loadFromMemory(generateToneWav({}), {});
    REQUIRE(handle.has_value());
    attachEmitter(world, ship, {.sound = *handle}, bank);

    TriggerQueue queue;
    std::thread producer([&queue, ship] {
        for (int i = 0; i < 50; ++i)
        {
            queue.push({.entityId = ship.id(), .volume = 1.0f});
        }
    });
    producer.join();
    CHECK(queue.size() == 50U);
    queue.flush(world, bank);
    CHECK(queue.size() == 0U);
    // Flushing dead entities is safe (skipped silently).
    queue.push({.entityId = 999999999ULL, .volume = 1.0f});
    queue.flush(world, bank);
    CHECK(queue.size() == 0U);
    REQUIRE(bank.stop(*handle));
}
