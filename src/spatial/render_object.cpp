#include "spatial/render_object.hpp"

namespace starforge::spatial
{

math::Mat4f RenderObject::cameraRelativeModelMatrix(const WorldPosition& camera) const noexcept
{
    return composeCameraRelativeModelMatrix(position, camera, localRotationScale);
}

math::Mat4f composeRenderObjectModelMatrix(const RenderObject& obj, const WorldPosition& camera) noexcept
{
    return obj.cameraRelativeModelMatrix(camera);
}

} // namespace starforge::spatial
