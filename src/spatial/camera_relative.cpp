#include "spatial/camera_relative.hpp"

namespace starforge::spatial
{

math::Vec3d toCameraRelative(const WorldPosition& pos, const WorldPosition& camera) noexcept
{
    return pos - camera;
}

math::Vec3f toFloat32(const math::Vec3d& v) noexcept
{
    return math::Vec3f{
        static_cast<float>(v.x),
        static_cast<float>(v.y),
        static_cast<float>(v.z),
    };
}

math::Mat4f toFloat32(const math::Mat4d& m) noexcept
{
    return math::Mat4f(m);
}

math::Mat4f composeCameraRelativeModelMatrix(
    const WorldPosition& objectPos,
    const WorldPosition& cameraPos,
    const math::Mat3d& localRotationScale) noexcept
{
    const math::Vec3d relTranslation = toCameraRelative(objectPos, cameraPos);

    math::Mat4f model(1.0f);
    model[0] = math::Vec4f(math::Vec3f(localRotationScale[0]), 0.0f);
    model[1] = math::Vec4f(math::Vec3f(localRotationScale[1]), 0.0f);
    model[2] = math::Vec4f(math::Vec3f(localRotationScale[2]), 0.0f);
    model[3] = math::Vec4f(toFloat32(relTranslation), 1.0f);

    return model;
}

math::Mat4f composeCameraRelativeModelView(
    const WorldPosition& objectPos,
    const WorldPosition& cameraPos,
    const math::Mat3d& localRotationScale,
    const math::Mat3d& viewRotation) noexcept
{
    const math::Vec3d relTranslation = toCameraRelative(objectPos, cameraPos);

    // Affine composition: R_mv = R_view * R_model, t_mv = R_view * t_rel.
    // Avoids full 4x4 matrix multiply (64 mul + 48 add -> 36 mul + 27 add).
    math::Mat3d rotationModelView = viewRotation * localRotationScale;
    math::Vec3d translationModelView = viewRotation * relTranslation;

    math::Mat4f modelView(1.0f);
    modelView[0] = math::Vec4f(math::Vec3f(rotationModelView[0]), 0.0f);
    modelView[1] = math::Vec4f(math::Vec3f(rotationModelView[1]), 0.0f);
    modelView[2] = math::Vec4f(math::Vec3f(rotationModelView[2]), 0.0f);
    modelView[3] = math::Vec4f(toFloat32(translationModelView), 1.0f);

    return modelView;
}

math::Vec3d composeModelViewTranslation(const WorldPosition& objectPos, const WorldPosition& cameraPos) noexcept
{
    return toCameraRelative(objectPos, cameraPos);
}

math::Vec3f LargeObjectSetup::transformPatchVertex(const math::Vec3f& patchLocalVertex) const noexcept
{
    const math::Vec3d vertexDouble{
        static_cast<double>(patchLocalVertex.x),
        static_cast<double>(patchLocalVertex.y),
        static_cast<double>(patchLocalVertex.z),
    };
    return toFloat32(cameraRelativeOrigin + vertexDouble);
}

math::Vec3f LargeObjectSetup::transformPlanetVertex(const math::Vec3d& planetRelativeVertex) const noexcept
{
    // cameraToObject is precomputed as objectCentre - camera.
    return toFloat32(cameraToObject + planetRelativeVertex);
}

LargeObjectSetup makeLargeObjectSetup(
    const WorldPosition& objectCentre,
    const WorldPosition& camera,
    const math::Vec3d& localOriginOffset) noexcept
{
    LargeObjectSetup setup;
    setup.objectCentre = objectCentre;
    setup.camera = camera;
    setup.localOriginOffset = localOriginOffset;
    setup.cameraToObject = toCameraRelative(objectCentre, camera);
    setup.cameraRelativeOrigin = setup.cameraToObject + localOriginOffset;
    return setup;
}

} // namespace starforge::spatial
