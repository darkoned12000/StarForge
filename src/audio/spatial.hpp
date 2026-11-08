#pragma once

// 3D spatialization reference math (M12.3): pure CPU functions with no
// miniaudio dependency, so the engine's spatial model is unit-testable
// headlessly. The miniaudio spatializer must match these models within
// tolerance (inverse-distance attenuation, stereo pan, Doppler).
//
// Conventions: right-handed, Y-up (matching starforge::math); positions
// and velocities are always camera-relative float32 by the time they
// reach here (see toCameraRelativeFloat below — the single-cast rule).

#include "math/math.hpp"
#include "spatial/camera_relative.hpp"
#include "spatial/world_position.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::audio
{

// Speed of sound in air (m/s), used by the CPU reference Doppler model.
inline constexpr float kSpeedOfSound = 343.0f;

// Inverse-distance-clamped attenuation matching
// ma_attenuation_model_inverse (OpenAL AL_INVERSE_DISTANCE_CLAMPED):
// full gain within refDistance, 1/d falloff beyond it. Past
// maxDistance the distance CLAMPS (it does not silence): the floor
// gain is ref/(ref + rolloff*(max-ref)), strictly positive. True
// silence past a range is a game-layer decision (stop the voice),
// not part of the model.
[[nodiscard]] inline float attenuationGain(float distance, float refDistance, float rolloff,
                                           float maxDistance) noexcept
{
    const float clampedDistance = std::max(distance, 0.0f);
    if (clampedDistance <= refDistance)
    {
        return 1.0f;
    }
    const float safeRolloff = std::max(rolloff, 0.0001f);
    const float safeRef = std::max(refDistance, 0.0001f);
    const float cappedDistance = std::min(clampedDistance, std::max(maxDistance, refDistance));
    const float gain = safeRef / (safeRef + safeRolloff * (cappedDistance - safeRef));
    return std::clamp(gain, 0.0f, 1.0f);
}

// Stereo pan in [-1, 1] from a listener-relative position: positive is
// right. Pan is the normalized X component (0 at the centre or at zero
// distance).
[[nodiscard]] inline float stereoPan(const math::Vec3f& listenerRelativePos) noexcept
{
    const float length = std::sqrt(listenerRelativePos.x * listenerRelativePos.x +
                                   listenerRelativePos.y * listenerRelativePos.y +
                                   listenerRelativePos.z * listenerRelativePos.z);
    if (length < 0.0001f)
    {
        return 0.0f;
    }
    return std::clamp(listenerRelativePos.x / length, -1.0f, 1.0f);
}

// Doppler pitch factor for a moving emitter and listener. Positive
// closing speed (emitter approaching) raises perceived pitch (> 1);
// receding lowers it (< 1). The factor is clamped to (0, 2] so it can
// never divide by zero (supersonic closure is unrepresentable in a
// linear Doppler model).
// radial = velocity of the emitter relative to the listener, projected
// on the listener->emitter direction; closing is radial < 0.
[[nodiscard]] inline float dopplerPitchFactor(const math::Vec3f& emitterPos,
                                              const math::Vec3f& emitterVel,
                                              const math::Vec3f& listenerPos,
                                              const math::Vec3f& listenerVel,
                                              float speedOfSound = kSpeedOfSound) noexcept
{
    const math::Vec3f toEmitter = emitterPos - listenerPos;
    const float distance = std::sqrt(toEmitter.x * toEmitter.x + toEmitter.y * toEmitter.y +
                                     toEmitter.z * toEmitter.z);
    if (distance < 0.0001f)
    {
        return 1.0f;
    }
    const math::Vec3f direction = toEmitter / distance;
    const math::Vec3f relativeVel = emitterVel - listenerVel;
    const float radial = relativeVel.x * direction.x + relativeVel.y * direction.y + relativeVel.z * direction.z;
    const float safeSpeed = std::max(speedOfSound, 0.0001f);
    if (radial <= -safeSpeed)
    {
        // Supersonic closure: the linear model breaks (shockwave);
        // saturate at maximum pitch-up instead of dividing by <= 0.
        return 2.0f;
    }
    const float factor = safeSpeed / (safeSpeed + radial);
    return std::clamp(factor, 0.1f, 2.0f);
}

// Batch camera-relative conversion: WorldPosition -> camera-relative
// float32 position (single-cast rule), and linear velocity double ->
// float32 (velocities are derivatives; magnitude is frame-local).
[[nodiscard]] inline math::Vec3f toCameraRelativeFloat(const spatial::WorldPosition& position,
                                                       const spatial::WorldPosition& camera) noexcept
{
    return spatial::toFloat32(spatial::toCameraRelative(position, camera));
}

[[nodiscard]] inline math::Vec3f cameraRelativeVelocity(const math::Vec3d& linearVelocity) noexcept
{
    return math::Vec3f{static_cast<float>(linearVelocity.x), static_cast<float>(linearVelocity.y),
                       static_cast<float>(linearVelocity.z)};
}

} // namespace starforge::audio
