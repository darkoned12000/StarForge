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

    math::Mat4d model(1.0);
    model[0] = math::Vec4d(localRotationScale[0], 0.0);
    model[1] = math::Vec4d(localRotationScale[1], 0.0);
    model[2] = math::Vec4d(localRotationScale[2], 0.0);
    model[3] = math::Vec4d(relTranslation, 1.0);

    return toFloat32(model);
}

math::Mat4f composeCameraRelativeModelView(
    const WorldPosition& objectPos,
    const WorldPosition& cameraPos,
    const math::Mat3d& localRotationScale,
    const math::Mat3d& viewRotation) noexcept
{
    const math::Vec3d relTranslation = toCameraRelative(objectPos, cameraPos);

    math::Mat4d model(1.0);
    model[0] = math::Vec4d(localRotationScale[0], 0.0);
    model[1] = math::Vec4d(localRotationScale[1], 0.0);
    model[2] = math::Vec4d(localRotationScale[2], 0.0);
    model[3] = math::Vec4d(relTranslation, 1.0);

    math::Mat4d view(1.0);
    view[0] = math::Vec4d(viewRotation[0], 0.0);
    view[1] = math::Vec4d(viewRotation[1], 0.0);
    view[2] = math::Vec4d(viewRotation[2], 0.0);

    const math::Mat4d modelView = view * model;
    return toFloat32(modelView);
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
