#pragma once

#include "math/math.hpp"
#include "spatial/world_position.hpp"

#include <cstdint>

namespace starforge::spatial
{

// Convert a world position to camera-relative metres in double precision.
[[nodiscard]] math::Vec3d toCameraRelative(const WorldPosition& pos, const WorldPosition& camera) noexcept;

// Cast a double-precision camera-relative vector to float32. This is the
// single point where precision is reduced; the input must be camera-relative
// (small magnitude) for the cast to be safe.
[[nodiscard]] math::Vec3f toFloat32(const math::Vec3d& v) noexcept;

// Cast a double-precision 4x4 matrix to float32 once after composing in double.
[[nodiscard]] math::Mat4f toFloat32(const math::Mat4d& m) noexcept;

// Compose a camera-relative model matrix in double precision, then cast to
// float32 once. `localRotationScale` holds the object's local orientation and
// scale around its own origin.
[[nodiscard]] math::Mat4f composeCameraRelativeModelMatrix(
    const WorldPosition& objectPos,
    const WorldPosition& cameraPos,
    const math::Mat3d& localRotationScale = math::Mat3d(1.0)) noexcept;

// Compose a full camera-relative Model-View matrix in double precision, then
// cast to float32 once. `viewRotation` is the camera's world-to-view rotation
// (no translation component, because the camera is at the origin of
// camera-relative space).
[[nodiscard]] math::Mat4f composeCameraRelativeModelView(
    const WorldPosition& objectPos,
    const WorldPosition& cameraPos,
    const math::Mat3d& localRotationScale,
    const math::Mat3d& viewRotation) noexcept;

// Backwards-compatible translation helper (returns camera-relative double offset).
[[nodiscard]] math::Vec3d composeModelViewTranslation(
    const WorldPosition& objectPos,
    const WorldPosition& cameraPos) noexcept;

// Large-object helper (e.g. a planet of radius 6.4e6 m).
// If vertices were stored relative to the planet's centre in float32, vertex
// spacing at radius 6.4e6 m would be ~0.5 m (visible surface jitter).
// Instead, patches/chunks use a `localOriginOffset` near the patch (in double,
// relative to `objectCentre`), and patch vertices are stored as small float32
// offsets relative to that local origin.
struct LargeObjectSetup
{
    WorldPosition objectCentre;
    WorldPosition camera;
    math::Vec3d localOriginOffset{0.0, 0.0, 0.0};    // patch origin relative to objectCentre (double)
    math::Vec3d cameraRelativeOrigin{0.0, 0.0, 0.0}; // (objectCentre + localOriginOffset) - camera (double)
    math::Vec3d cameraToObject{0.0, 0.0, 0.0};       // objectCentre - camera (double), precomputed

    // Transforms a local vertex (in metres relative to `localOriginOffset`)
    // into camera-relative float32 space without precision loss.
    [[nodiscard]] math::Vec3f transformPatchVertex(const math::Vec3f& patchLocalVertex) const noexcept;

    // Transforms a planet-centred double vertex into camera-relative float32
    // space by composing in double first and casting to float32 once.
    // Uses the precomputed cameraToObject vector.
    [[nodiscard]] math::Vec3f transformPlanetVertex(const math::Vec3d& planetRelativeVertex) const noexcept;
};

[[nodiscard]] LargeObjectSetup makeLargeObjectSetup(
    const WorldPosition& objectCentre,
    const WorldPosition& camera,
    const math::Vec3d& localOriginOffset = math::Vec3d{0.0, 0.0, 0.0}) noexcept;

} // namespace starforge::spatial
