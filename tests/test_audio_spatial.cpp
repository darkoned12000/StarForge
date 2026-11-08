// Audio 3D spatialization tests (M12.3): CPU reference math and the
// miniaudio listener/emitter forwarding. Null backend, no hardware.
// Positions are always camera-relative float32 (single-cast rule).
#include <doctest/doctest.h>

#include "audio/audio_context.hpp"
#include "audio/sound.hpp"
#include "audio/spatial.hpp"
#include "audio/tone_generator.hpp"

#include <cmath>
#include <string>

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

TEST_CASE("Spatial: attenuation is monotone with distance")
{
    // Reference 1 m, rolloff 1, distance clamped at 100 m.
    CHECK(attenuationGain(0.5f, 1.0f, 1.0f, 100.0f) == doctest::Approx(1.0f));
    CHECK(attenuationGain(1.0f, 1.0f, 1.0f, 100.0f) == doctest::Approx(1.0f));
    const float mid = attenuationGain(10.0f, 1.0f, 1.0f, 100.0f);
    const float far = attenuationGain(50.0f, 1.0f, 1.0f, 100.0f);
    const float veryFar = attenuationGain(99.0f, 1.0f, 1.0f, 100.0f);
    CHECK(mid < 1.0f);
    CHECK(mid > far);
    CHECK(far > veryFar);
    CHECK(veryFar > 0.0f);
    // miniaudio/OpenAL clamped model: distance stops decreasing past
    // maxDistance at a positive floor gain, it never silences.
    const float atMax = attenuationGain(100.0f, 1.0f, 1.0f, 100.0f);
    CHECK(atMax > 0.0f);
    CHECK(atMax < 1.0f);
    CHECK(attenuationGain(500.0f, 1.0f, 1.0f, 100.0f) == doctest::Approx(atMax));
    // A larger reference distance raises the floor (less attenuation).
    CHECK(attenuationGain(100.0f, 10.0f, 1.0f, 100.0f) > atMax);
    // Degenerate inputs are safe.
    CHECK(attenuationGain(10.0f, 0.0f, 0.0f, 100.0f) >= 0.0f);
    CHECK(attenuationGain(10.0f, 0.0f, 0.0f, 100.0f) <= 1.0f);
}

TEST_CASE("Spatial: stereo pan sign follows horizontal position")
{
    CHECK(stereoPan({0.0f, 0.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(stereoPan({0.0f, 0.0f, -10.0f}) == doctest::Approx(0.0f));
    CHECK(stereoPan({10.0f, 0.0f, 0.0f}) > 0.9f);
    CHECK(stereoPan({-10.0f, 0.0f, 0.0f}) < -0.9f);
    CHECK(stereoPan({1.0f, 0.0f, -1.0f}) > 0.0f);
    CHECK(stereoPan({1.0f, 0.0f, -1.0f}) == doctest::Approx(0.7071f).epsilon(0.01));
    // Pan magnitude never exceeds 1 (diagonals, verticals).
    CHECK(std::abs(stereoPan({0.0f, 10.0f, 0.0f})) < 0.01f);
}

TEST_CASE("Spatial: Doppler pitch rises on approach, falls on recession")
{
    const starforge::math::Vec3f listenerPos{0.0f, 0.0f, 0.0f};
    const starforge::math::Vec3f listenerVel{0.0f, 0.0f, 0.0f};
    const starforge::math::Vec3f emitterPos{100.0f, 0.0f, 0.0f};

    // Emitter at +X moving toward the listener (-X velocity).
    const starforge::math::Vec3f approachVel{-100.0f, 0.0f, 0.0f};
    const starforge::math::Vec3f recedeVel{100.0f, 0.0f, 0.0f};
    const starforge::math::Vec3f stillVel{0.0f, 0.0f, 0.0f};

    CHECK(dopplerPitchFactor(emitterPos, approachVel, listenerPos, listenerVel) > 1.0f);
    CHECK(dopplerPitchFactor(emitterPos, recedeVel, listenerPos, listenerVel) < 1.0f);
    CHECK(dopplerPitchFactor(emitterPos, stillVel, listenerPos, listenerVel) == doctest::Approx(1.0f));

    // Listener motion counts too: listener chasing the emitter pitches up.
    CHECK(dopplerPitchFactor(emitterPos, stillVel, listenerPos, {100.0f, 0.0f, 0.0f}) > 1.0f);

    // Zero distance and supersonic closure are clamped, never NaN/inf.
    CHECK(dopplerPitchFactor(emitterPos, approachVel, emitterPos, listenerVel) == doctest::Approx(1.0f));
    const float supersonic = dopplerPitchFactor(emitterPos, {-1000.0f, 0.0f, 0.0f}, listenerPos, listenerVel);
    CHECK(std::isfinite(supersonic));
    CHECK(supersonic >= 0.0f);
    CHECK(supersonic <= 2.0f);
    // Supersonic closure saturates at the clamp ceiling, not infinity.
    CHECK(supersonic == doctest::Approx(2.0f));
}

TEST_CASE("Spatial: flyby pitch peaks at closest approach")
{
    // Emitter flies from +X to -X through (0, 10, 0); listener at origin.
    const starforge::math::Vec3f listenerPos{0.0f, 0.0f, 0.0f};
    const starforge::math::Vec3f listenerVel{0.0f, 0.0f, 0.0f};
    const starforge::math::Vec3f velocity{-100.0f, 0.0f, 0.0f};
    const float before = dopplerPitchFactor({300.0f, 10.0f, 0.0f}, velocity, listenerPos, listenerVel);
    const float closest = dopplerPitchFactor({0.0f, 10.0f, 0.0f}, velocity, listenerPos, listenerVel);
    const float after = dopplerPitchFactor({-300.0f, 10.0f, 0.0f}, velocity, listenerPos, listenerVel);
    CHECK(before > 1.0f);
    CHECK(after < 1.0f);
    // Closing speed is maximal at flyby start and zero at closest
    // approach (velocity is perpendicular to the line of sight there).
    CHECK(before > closest);
    CHECK(closest > after);
}

TEST_CASE("Spatial: emitter pose round-trips through miniaudio")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    auto handle = bank.loadFromMemory(generateToneWav({}), {});
    REQUIRE(handle.has_value());
    REQUIRE(bank.setEmitterPose(*handle, {10.0f, -2.0f, 5.0f}, {1.0f, 0.0f, -1.0f}, 0.8f));
    const auto pos = bank.emitterPosition(*handle);
    CHECK(pos.x == doctest::Approx(10.0f));
    CHECK(pos.y == doctest::Approx(-2.0f));
    CHECK(pos.z == doctest::Approx(5.0f));
    const auto vel = bank.emitterVelocity(*handle);
    CHECK(vel.x == doctest::Approx(1.0f));
    CHECK(vel.z == doctest::Approx(-1.0f));
    CHECK(bank.emitterDopplerFactor(*handle) == doctest::Approx(0.8f));

    // 2D buses reject emitter poses.
    SoundConfig uiConfig;
    uiConfig.bus = Bus::UI;
    auto uiHandle = bank.loadFromMemory(generateToneWav({}), uiConfig);
    REQUIRE(uiHandle.has_value());
    CHECK(!bank.setEmitterPose(*uiHandle, {0.0f, 0.0f, 0.0f}, {}, 1.0f));
    CHECK(!bank.setEmitterDopplerFactor(*uiHandle, 2.0f));
}

TEST_CASE("Spatial: listener pose round-trips through miniaudio")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    REQUIRE(context->setListenerPose({3.0f, 1.0f, -4.0f}, {0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
                                     {0.0f, 1.0f, 0.0f}));
    const auto pos = context->listenerPosition();
    CHECK(pos.x == doctest::Approx(3.0f));
    CHECK(pos.y == doctest::Approx(1.0f));
    CHECK(pos.z == doctest::Approx(-4.0f));
    CHECK(context->listenerVelocity().x == doctest::Approx(0.5f));
    CHECK(context->listenerDirection().z == doctest::Approx(-1.0f));
}

TEST_CASE("Spatial: far world position gives sane camera-relative params")
{
    using namespace starforge::spatial;
    const auto camera = WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto emitter = WorldPosition::fromMeters(1.0e12, 100.0, -50.0);
    REQUIRE(camera.has_value());
    REQUIRE(emitter.has_value());
    // Camera-relative gap is ~100 m in Y, -50 m in Z (X direction is
    // dominated by the 1e12 offset, Y/Z stay precise).
    const auto relative = toCameraRelative(*emitter, *camera);
    CHECK(relative.y == doctest::Approx(100.0).epsilon(0.001));
    CHECK(relative.z == doctest::Approx(-50.0).epsilon(0.001));
    CHECK(std::isfinite(relative.x));
    // A nearby emitter at interstellar range keeps its precise local
    // geometry (the precision invariant the spatialization relies on).
    const auto nearby = emitter->translated({10.0, 0.0, 0.0});
    const auto nearbyRel = toCameraRelative(*nearby, *camera);
    CHECK(nearbyRel.x - relative.x == doctest::Approx(10.0).epsilon(0.001));
}
