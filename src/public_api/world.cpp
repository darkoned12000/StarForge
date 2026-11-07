#include "starforge/world.hpp"

#include "world_bridge.hpp"

namespace starforge
{
namespace
{
namespace bridge = public_api_bridge;

[[nodiscard]] bridge::RawWorldPosition toRaw(const WorldPosition& value) noexcept
{
    return bridge::RawWorldPosition{value.cellX, value.cellY, value.cellZ, value.offsetX, value.offsetY, value.offsetZ};
}

[[nodiscard]] WorldPosition fromRaw(const bridge::RawWorldPosition& value) noexcept
{
    WorldPosition result;
    result.cellX = value.cellX;
    result.cellY = value.cellY;
    result.cellZ = value.cellZ;
    result.offsetX = value.offsetX;
    result.offsetY = value.offsetY;
    result.offsetZ = value.offsetZ;
    return result;
}

[[nodiscard]] bridge::RawVec3d toRaw(const math::Vec3d& value) noexcept
{
    return bridge::RawVec3d{value.x(), value.y(), value.z()};
}

[[nodiscard]] math::Vec3d fromRaw(const bridge::RawVec3d& value) noexcept
{
    return math::Vec3d{value.x, value.y, value.z};
}

[[nodiscard]] bridge::RawVec3f toRaw(const math::Vec3f& value) noexcept
{
    return bridge::RawVec3f{value.x(), value.y(), value.z()};
}

[[nodiscard]] math::Vec3f fromRaw(const bridge::RawVec3f& value) noexcept
{
    return math::Vec3f{value.x, value.y, value.z};
}

[[nodiscard]] bridge::RawMat3d toRaw(const math::Mat3d& value) noexcept
{
    bridge::RawMat3d result;
    for (std::size_t column = 0; column < 3; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            result.columns[column][row] = value.columns[column][row];
        }
    }
    return result;
}

[[nodiscard]] math::Mat4f fromRaw(const bridge::RawMat4f& value) noexcept
{
    math::Mat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result.columns[column][row] = value.columns[column][row];
        }
    }
    return result;
}

[[nodiscard]] bridge::RawLargeObjectSetup toRaw(const LargeObjectSetup& value) noexcept
{
    return bridge::RawLargeObjectSetup{
        toRaw(value.objectCentre), toRaw(value.camera), toRaw(value.localOriginOffset),
        toRaw(value.cameraRelativeOrigin), toRaw(value.cameraToObject)};
}

template <class T>
[[nodiscard]] std::expected<WorldPosition, std::string> mapExpected(std::expected<T, std::string>&& value)
{
    if (!value)
    {
        return std::unexpected(value.error());
    }
    return fromRaw(*value);
}

} // namespace

std::expected<WorldPosition, std::string> WorldPosition::fromCellAndOffset(
    std::int64_t cx, std::int64_t cy, std::int64_t cz, double ox, double oy, double oz)
{
    return mapExpected(bridge::fromCellAndOffset(cx, cy, cz, ox, oy, oz));
}

std::expected<WorldPosition, std::string> WorldPosition::fromMeters(double x, double y, double z)
{
    return mapExpected(bridge::fromMeters(x, y, z));
}

std::expected<WorldPosition, std::string> WorldPosition::fromMeters(const math::Vec3d& position)
{
    return mapExpected(bridge::fromMeters(toRaw(position)));
}

math::Vec3d WorldPosition::toMeters() const noexcept
{
    return fromRaw(bridge::toMeters(toRaw(*this)));
}

std::expected<WorldPosition, std::string> WorldPosition::shiftedByCells(
    std::int64_t dCellX, std::int64_t dCellY, std::int64_t dCellZ) const
{
    return mapExpected(bridge::shiftedByCells(toRaw(*this), dCellX, dCellY, dCellZ));
}

std::expected<WorldPosition, std::string> WorldPosition::translated(const math::Vec3d& delta) const
{
    return mapExpected(bridge::translated(toRaw(*this), toRaw(delta)));
}

WorldPosition& WorldPosition::operator+=(const math::Vec3d& delta)
{
    *this = fromRaw(bridge::addDelta(toRaw(*this), toRaw(delta)));
    return *this;
}

WorldPosition& WorldPosition::operator-=(const math::Vec3d& delta)
{
    *this = fromRaw(bridge::subDelta(toRaw(*this), toRaw(delta)));
    return *this;
}

WorldPosition WorldPosition::operator+(const math::Vec3d& delta) const
{
    WorldPosition result = *this;
    result += delta;
    return result;
}

WorldPosition WorldPosition::operator-(const math::Vec3d& delta) const
{
    WorldPosition result = *this;
    result -= delta;
    return result;
}

WorldPosition operator+(const math::Vec3d& delta, const WorldPosition& position)
{
    return position + delta;
}

math::Vec3d WorldPosition::operator-(const WorldPosition& other) const noexcept
{
    return fromRaw(bridge::subtractPositions(toRaw(*this), toRaw(other)));
}

bool WorldPosition::operator==(const WorldPosition& other) const noexcept
{
    return bridge::equalPositions(toRaw(*this), toRaw(other));
}

bool WorldPosition::operator!=(const WorldPosition& other) const noexcept
{
    return !(*this == other);
}

std::uint64_t WorldPosition::cellHash() const noexcept
{
    return bridge::cellHash(toRaw(*this));
}

std::expected<bool, std::string> WorldPosition::normalize()
{
    auto raw = toRaw(*this);
    auto result = bridge::normalize(raw);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    *this = fromRaw(raw);
    return *result;
}

bool WorldPosition::isNormalized() const noexcept
{
    return bridge::isNormalized(toRaw(*this));
}

std::int64_t WorldPosition::coarseCellX(int level) const noexcept
{
    return bridge::coarseCellX(toRaw(*this), level);
}

std::int64_t WorldPosition::coarseCellY(int level) const noexcept
{
    return bridge::coarseCellY(toRaw(*this), level);
}

std::int64_t WorldPosition::coarseCellZ(int level) const noexcept
{
    return bridge::coarseCellZ(toRaw(*this), level);
}

std::int64_t WorldPosition::coarseCellByBitsX(int shiftBits) const noexcept
{
    return bridge::coarseCellByBitsX(toRaw(*this), shiftBits);
}

std::int64_t WorldPosition::coarseCellByBitsY(int shiftBits) const noexcept
{
    return bridge::coarseCellByBitsY(toRaw(*this), shiftBits);
}

std::int64_t WorldPosition::coarseCellByBitsZ(int shiftBits) const noexcept
{
    return bridge::coarseCellByBitsZ(toRaw(*this), shiftBits);
}

math::Vec3d toCameraRelative(const WorldPosition& position, const WorldPosition& camera) noexcept
{
    return position - camera;
}

math::Vec3f toFloat32(const math::Vec3d& value) noexcept
{
    return fromRaw(bridge::toFloat32Vec(toRaw(value)));
}

math::Mat4f toFloat32(const math::Mat4d& matrix) noexcept
{
    // Pure element-wise cast implemented in the public math header.
    return math::toFloat32(matrix);
}

math::Mat4f composeCameraRelativeModelMatrix(
    const WorldPosition& objectPosition, const WorldPosition& cameraPosition, const math::Mat3d& localRotationScale) noexcept
{
    return fromRaw(bridge::composeCameraRelativeModelMatrix(toRaw(objectPosition), toRaw(cameraPosition), toRaw(localRotationScale)));
}

math::Mat4f composeCameraRelativeModelView(
    const WorldPosition& objectPosition, const WorldPosition& cameraPosition,
    const math::Mat3d& localRotationScale, const math::Mat3d& viewRotation) noexcept
{
    return fromRaw(bridge::composeCameraRelativeModelView(
        toRaw(objectPosition), toRaw(cameraPosition), toRaw(localRotationScale), toRaw(viewRotation)));
}

math::Vec3d composeModelViewTranslation(const WorldPosition& objectPosition, const WorldPosition& cameraPosition) noexcept
{
    return fromRaw(bridge::composeModelViewTranslation(toRaw(objectPosition), toRaw(cameraPosition)));
}

math::Vec3f LargeObjectSetup::transformPatchVertex(const math::Vec3f& patchLocalVertex) const noexcept
{
    return fromRaw(bridge::transformPatchVertex(toRaw(*this), toRaw(patchLocalVertex)));
}

math::Vec3f LargeObjectSetup::transformPlanetVertex(const math::Vec3d& planetRelativeVertex) const noexcept
{
    return fromRaw(bridge::transformPlanetVertex(toRaw(*this), toRaw(planetRelativeVertex)));
}

LargeObjectSetup makeLargeObjectSetup(
    const WorldPosition& objectCentre, const WorldPosition& camera, const math::Vec3d& localOriginOffset) noexcept
{
    const auto raw = bridge::makeLargeObjectSetup(toRaw(objectCentre), toRaw(camera), toRaw(localOriginOffset));
    LargeObjectSetup result;
    result.objectCentre = fromRaw(raw.objectCentre);
    result.camera = fromRaw(raw.camera);
    result.localOriginOffset = fromRaw(raw.localOriginOffset);
    result.cameraRelativeOrigin = fromRaw(raw.cameraRelativeOrigin);
    result.cameraToObject = fromRaw(raw.cameraToObject);
    return result;
}

} // namespace starforge
