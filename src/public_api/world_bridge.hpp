#pragma once

#include <cstdint>
#include <expected>
#include <string>

// Internal bridge between the public world API and the spatial backend.
// Uses only plain data so no translation unit ever includes both the public
// math header and the internal GLM-based math aliases (both live in
// starforge::math). This header must not be installed.
namespace starforge::public_api_bridge
{

struct RawVec3d
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct RawVec3f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RawMat3d
{
    double columns[3][3] = {};
};

struct RawMat4f
{
    float columns[4][4] = {};
};

struct RawWorldPosition
{
    std::int64_t cellX = 0;
    std::int64_t cellY = 0;
    std::int64_t cellZ = 0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double offsetZ = 0.0;
};

struct RawLargeObjectSetup
{
    RawWorldPosition objectCentre;
    RawWorldPosition camera;
    RawVec3d localOriginOffset;
    RawVec3d cameraRelativeOrigin;
    RawVec3d cameraToObject;
};

[[nodiscard]] std::expected<RawWorldPosition, std::string>
fromCellAndOffset(std::int64_t cx, std::int64_t cy, std::int64_t cz, double ox, double oy, double oz);
[[nodiscard]] std::expected<RawWorldPosition, std::string> fromMeters(double x, double y, double z);
[[nodiscard]] std::expected<RawWorldPosition, std::string> fromMeters(const RawVec3d& position);
[[nodiscard]] RawVec3d toMeters(const RawWorldPosition& position) noexcept;
[[nodiscard]] std::expected<RawWorldPosition, std::string>
shiftedByCells(const RawWorldPosition& position, std::int64_t dx, std::int64_t dy, std::int64_t dz);
[[nodiscard]] std::expected<RawWorldPosition, std::string>
translated(const RawWorldPosition& position, const RawVec3d& delta);
[[nodiscard]] RawWorldPosition addDelta(const RawWorldPosition& position, const RawVec3d& delta) noexcept;
[[nodiscard]] RawWorldPosition subDelta(const RawWorldPosition& position, const RawVec3d& delta) noexcept;
[[nodiscard]] RawVec3d subtractPositions(const RawWorldPosition& lhs, const RawWorldPosition& rhs) noexcept;
[[nodiscard]] bool equalPositions(const RawWorldPosition& lhs, const RawWorldPosition& rhs) noexcept;
[[nodiscard]] std::uint64_t cellHash(const RawWorldPosition& position) noexcept;
[[nodiscard]] std::expected<bool, std::string> normalize(RawWorldPosition& position);
[[nodiscard]] bool isNormalized(const RawWorldPosition& position) noexcept;
[[nodiscard]] std::int64_t coarseCellX(const RawWorldPosition& position, int level) noexcept;
[[nodiscard]] std::int64_t coarseCellY(const RawWorldPosition& position, int level) noexcept;
[[nodiscard]] std::int64_t coarseCellZ(const RawWorldPosition& position, int level) noexcept;
[[nodiscard]] std::int64_t coarseCellByBitsX(const RawWorldPosition& position, int shiftBits) noexcept;
[[nodiscard]] std::int64_t coarseCellByBitsY(const RawWorldPosition& position, int shiftBits) noexcept;
[[nodiscard]] std::int64_t coarseCellByBitsZ(const RawWorldPosition& position, int shiftBits) noexcept;

[[nodiscard]] RawVec3f toFloat32Vec(const RawVec3d& value) noexcept;
[[nodiscard]] RawMat4f composeCameraRelativeModelMatrix(
    const RawWorldPosition& objectPosition, const RawWorldPosition& cameraPosition, const RawMat3d& localRotationScale) noexcept;
[[nodiscard]] RawMat4f composeCameraRelativeModelView(
    const RawWorldPosition& objectPosition, const RawWorldPosition& cameraPosition,
    const RawMat3d& localRotationScale, const RawMat3d& viewRotation) noexcept;
[[nodiscard]] RawVec3d composeModelViewTranslation(
    const RawWorldPosition& objectPosition, const RawWorldPosition& cameraPosition) noexcept;

[[nodiscard]] RawVec3f transformPatchVertex(const RawLargeObjectSetup& setup, const RawVec3f& vertex) noexcept;
[[nodiscard]] RawVec3f transformPlanetVertex(const RawLargeObjectSetup& setup, const RawVec3d& vertex) noexcept;
[[nodiscard]] RawLargeObjectSetup
makeLargeObjectSetup(const RawWorldPosition& objectCentre, const RawWorldPosition& camera, const RawVec3d& offset) noexcept;

} // namespace starforge::public_api_bridge
