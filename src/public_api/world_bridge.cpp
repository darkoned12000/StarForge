#include "world_bridge.hpp"

#include "spatial/camera_relative.hpp"
#include "spatial/world_position.hpp"

namespace starforge::public_api_bridge
{
namespace
{

[[nodiscard]] spatial::WorldPosition toInternal(const RawWorldPosition& value) noexcept
{
    spatial::WorldPosition result;
    result.cellX = value.cellX;
    result.cellY = value.cellY;
    result.cellZ = value.cellZ;
    result.offsetX = value.offsetX;
    result.offsetY = value.offsetY;
    result.offsetZ = value.offsetZ;
    return result;
}

[[nodiscard]] RawWorldPosition fromInternal(const spatial::WorldPosition& value) noexcept
{
    RawWorldPosition result;
    result.cellX = value.cellX;
    result.cellY = value.cellY;
    result.cellZ = value.cellZ;
    result.offsetX = value.offsetX;
    result.offsetY = value.offsetY;
    result.offsetZ = value.offsetZ;
    return result;
}

[[nodiscard]] ::starforge::math::Vec3d toInternalVec(const RawVec3d& value) noexcept
{
    return ::starforge::math::Vec3d{value.x, value.y, value.z};
}

[[nodiscard]] RawVec3d fromInternalVec(const ::starforge::math::Vec3d& value) noexcept
{
    return RawVec3d{value.x, value.y, value.z};
}

[[nodiscard]] ::starforge::math::Vec3f toInternalVecF(const RawVec3f& value) noexcept
{
    return ::starforge::math::Vec3f{value.x, value.y, value.z};
}

[[nodiscard]] RawVec3f fromInternalVecF(const ::starforge::math::Vec3f& value) noexcept
{
    return RawVec3f{value.x, value.y, value.z};
}

[[nodiscard]] ::starforge::math::Mat3d toInternalMat3(const RawMat3d& value) noexcept
{
    ::starforge::math::Mat3d result;
    for (std::size_t column = 0; column < 3; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            result[static_cast<int>(column)][static_cast<int>(row)] = value.columns[column][row];
        }
    }
    return result;
}

[[nodiscard]] RawMat4f fromInternalMat4(const ::starforge::math::Mat4f& value) noexcept
{
    RawMat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result.columns[column][row] = value[static_cast<int>(column)][static_cast<int>(row)];
        }
    }
    return result;
}

[[nodiscard]] spatial::LargeObjectSetup toInternalSetup(const RawLargeObjectSetup& value) noexcept
{
    spatial::LargeObjectSetup result;
    result.objectCentre = toInternal(value.objectCentre);
    result.camera = toInternal(value.camera);
    result.localOriginOffset = toInternalVec(value.localOriginOffset);
    result.cameraRelativeOrigin = toInternalVec(value.cameraRelativeOrigin);
    result.cameraToObject = toInternalVec(value.cameraToObject);
    return result;
}

[[nodiscard]] RawLargeObjectSetup fromInternalSetup(const spatial::LargeObjectSetup& value) noexcept
{
    RawLargeObjectSetup result;
    result.objectCentre = fromInternal(value.objectCentre);
    result.camera = fromInternal(value.camera);
    result.localOriginOffset = fromInternalVec(value.localOriginOffset);
    result.cameraRelativeOrigin = fromInternalVec(value.cameraRelativeOrigin);
    result.cameraToObject = fromInternalVec(value.cameraToObject);
    return result;
}

template <class T>
[[nodiscard]] std::expected<RawWorldPosition, std::string> mapExpected(std::expected<T, std::string>&& value)
{
    if (!value)
    {
        return std::unexpected(value.error());
    }
    return fromInternal(*value);
}

} // namespace

std::expected<RawWorldPosition, std::string>
fromCellAndOffset(std::int64_t cx, std::int64_t cy, std::int64_t cz, double ox, double oy, double oz)
{
    return mapExpected(spatial::WorldPosition::fromCellAndOffset(cx, cy, cz, ox, oy, oz));
}

std::expected<RawWorldPosition, std::string> fromMeters(double x, double y, double z)
{
    return mapExpected(spatial::WorldPosition::fromMeters(x, y, z));
}

std::expected<RawWorldPosition, std::string> fromMeters(const RawVec3d& position)
{
    return mapExpected(spatial::WorldPosition::fromMeters(toInternalVec(position)));
}

RawVec3d toMeters(const RawWorldPosition& position) noexcept
{
    return fromInternalVec(toInternal(position).toMeters());
}

std::expected<RawWorldPosition, std::string>
shiftedByCells(const RawWorldPosition& position, std::int64_t dx, std::int64_t dy, std::int64_t dz)
{
    return mapExpected(toInternal(position).shiftedByCells(dx, dy, dz));
}

std::expected<RawWorldPosition, std::string> translated(const RawWorldPosition& position, const RawVec3d& delta)
{
    return mapExpected(toInternal(position).translated(toInternalVec(delta)));
}

RawWorldPosition addDelta(const RawWorldPosition& position, const RawVec3d& delta) noexcept
{
    auto result = toInternal(position);
    result += toInternalVec(delta);
    return fromInternal(result);
}

RawWorldPosition subDelta(const RawWorldPosition& position, const RawVec3d& delta) noexcept
{
    auto result = toInternal(position);
    result -= toInternalVec(delta);
    return fromInternal(result);
}

RawVec3d subtractPositions(const RawWorldPosition& lhs, const RawWorldPosition& rhs) noexcept
{
    return fromInternalVec(toInternal(lhs) - toInternal(rhs));
}

bool equalPositions(const RawWorldPosition& lhs, const RawWorldPosition& rhs) noexcept
{
    return toInternal(lhs) == toInternal(rhs);
}

std::uint64_t cellHash(const RawWorldPosition& position) noexcept
{
    return toInternal(position).cellHash();
}

std::expected<bool, std::string> normalize(RawWorldPosition& position)
{
    auto internal = toInternal(position);
    auto result = internal.normalize();
    if (!result)
    {
        return std::unexpected(result.error());
    }
    position = fromInternal(internal);
    return *result;
}

bool isNormalized(const RawWorldPosition& position) noexcept
{
    return toInternal(position).isNormalized();
}

std::int64_t coarseCellX(const RawWorldPosition& position, int level) noexcept
{
    return toInternal(position).coarseCellX(level);
}

std::int64_t coarseCellY(const RawWorldPosition& position, int level) noexcept
{
    return toInternal(position).coarseCellY(level);
}

std::int64_t coarseCellZ(const RawWorldPosition& position, int level) noexcept
{
    return toInternal(position).coarseCellZ(level);
}

std::int64_t coarseCellByBitsX(const RawWorldPosition& position, int shiftBits) noexcept
{
    return toInternal(position).coarseCellByBitsX(shiftBits);
}

std::int64_t coarseCellByBitsY(const RawWorldPosition& position, int shiftBits) noexcept
{
    return toInternal(position).coarseCellByBitsY(shiftBits);
}

std::int64_t coarseCellByBitsZ(const RawWorldPosition& position, int shiftBits) noexcept
{
    return toInternal(position).coarseCellByBitsZ(shiftBits);
}

RawVec3f toFloat32Vec(const RawVec3d& value) noexcept
{
    return fromInternalVecF(spatial::toFloat32(toInternalVec(value)));
}

RawMat4f composeCameraRelativeModelMatrix(
    const RawWorldPosition& objectPosition, const RawWorldPosition& cameraPosition, const RawMat3d& localRotationScale) noexcept
{
    return fromInternalMat4(spatial::composeCameraRelativeModelMatrix(
        toInternal(objectPosition), toInternal(cameraPosition), toInternalMat3(localRotationScale)));
}

RawMat4f composeCameraRelativeModelView(
    const RawWorldPosition& objectPosition, const RawWorldPosition& cameraPosition,
    const RawMat3d& localRotationScale, const RawMat3d& viewRotation) noexcept
{
    return fromInternalMat4(spatial::composeCameraRelativeModelView(
        toInternal(objectPosition), toInternal(cameraPosition), toInternalMat3(localRotationScale),
        toInternalMat3(viewRotation)));
}

RawVec3d composeModelViewTranslation(const RawWorldPosition& objectPosition, const RawWorldPosition& cameraPosition) noexcept
{
    return fromInternalVec(spatial::composeModelViewTranslation(toInternal(objectPosition), toInternal(cameraPosition)));
}

RawVec3f transformPatchVertex(const RawLargeObjectSetup& setup, const RawVec3f& vertex) noexcept
{
    return fromInternalVecF(toInternalSetup(setup).transformPatchVertex(toInternalVecF(vertex)));
}

RawVec3f transformPlanetVertex(const RawLargeObjectSetup& setup, const RawVec3d& vertex) noexcept
{
    return fromInternalVecF(toInternalSetup(setup).transformPlanetVertex(toInternalVec(vertex)));
}

RawLargeObjectSetup makeLargeObjectSetup(const RawWorldPosition& objectCentre, const RawWorldPosition& camera, const RawVec3d& offset) noexcept
{
    return fromInternalSetup(spatial::makeLargeObjectSetup(toInternal(objectCentre), toInternal(camera), toInternalVec(offset)));
}

} // namespace starforge::public_api_bridge
