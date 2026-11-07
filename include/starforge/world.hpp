#pragma once

#include "starforge/math.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace starforge
{

struct WorldPosition
{
    static constexpr int kCellBits = 20;
    static constexpr double kCellSize = static_cast<double>(1LL << kCellBits);
    static constexpr double kMaxReach = 9.2233720368547748e18 * kCellSize;

    std::int64_t cellX = 0;
    std::int64_t cellY = 0;
    std::int64_t cellZ = 0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double offsetZ = 0.0;

    [[nodiscard]] static std::expected<WorldPosition, std::string>
    fromCellAndOffset(std::int64_t cx, std::int64_t cy, std::int64_t cz,
                      double ox = 0.0, double oy = 0.0, double oz = 0.0);
    [[nodiscard]] static std::expected<WorldPosition, std::string> fromMeters(double x, double y, double z);
    [[nodiscard]] static std::expected<WorldPosition, std::string> fromMeters(const math::Vec3d& position);

    [[nodiscard]] math::Vec3d toMeters() const noexcept;
    [[nodiscard]] std::expected<WorldPosition, std::string>
    shiftedByCells(std::int64_t dCellX, std::int64_t dCellY, std::int64_t dCellZ) const;
    [[nodiscard]] std::expected<WorldPosition, std::string> translated(const math::Vec3d& delta) const;

    WorldPosition& operator+=(const math::Vec3d& delta);
    WorldPosition& operator-=(const math::Vec3d& delta);
    [[nodiscard]] WorldPosition operator+(const math::Vec3d& delta) const;
    [[nodiscard]] WorldPosition operator-(const math::Vec3d& delta) const;
    friend WorldPosition operator+(const math::Vec3d& delta, const WorldPosition& position);
    [[nodiscard]] math::Vec3d operator-(const WorldPosition& other) const noexcept;

    [[nodiscard]] bool operator==(const WorldPosition& other) const noexcept;
    [[nodiscard]] bool operator!=(const WorldPosition& other) const noexcept;
    [[nodiscard]] std::uint64_t cellHash() const noexcept;
    [[nodiscard]] std::expected<bool, std::string> normalize();
    [[nodiscard]] bool isNormalized() const noexcept;

    [[nodiscard]] std::int64_t coarseCellX(int level) const noexcept;
    [[nodiscard]] std::int64_t coarseCellY(int level) const noexcept;
    [[nodiscard]] std::int64_t coarseCellZ(int level) const noexcept;
    [[nodiscard]] std::int64_t coarseCellByBitsX(int shiftBits) const noexcept;
    [[nodiscard]] std::int64_t coarseCellByBitsY(int shiftBits) const noexcept;
    [[nodiscard]] std::int64_t coarseCellByBitsZ(int shiftBits) const noexcept;
};

[[nodiscard]] math::Vec3d toCameraRelative(const WorldPosition& position, const WorldPosition& camera) noexcept;
[[nodiscard]] math::Vec3f toFloat32(const math::Vec3d& value) noexcept;
[[nodiscard]] math::Mat4f toFloat32(const math::Mat4d& matrix) noexcept;
[[nodiscard]] math::Mat4f composeCameraRelativeModelMatrix(
    const WorldPosition& objectPosition, const WorldPosition& cameraPosition,
    const math::Mat3d& localRotationScale = math::Mat3d(1.0)) noexcept;
[[nodiscard]] math::Mat4f composeCameraRelativeModelView(
    const WorldPosition& objectPosition, const WorldPosition& cameraPosition,
    const math::Mat3d& localRotationScale, const math::Mat3d& viewRotation) noexcept;
[[nodiscard]] math::Vec3d composeModelViewTranslation(
    const WorldPosition& objectPosition, const WorldPosition& cameraPosition) noexcept;

struct LargeObjectSetup
{
    WorldPosition objectCentre;
    WorldPosition camera;
    math::Vec3d localOriginOffset{0.0, 0.0, 0.0};
    math::Vec3d cameraRelativeOrigin{0.0, 0.0, 0.0};
    math::Vec3d cameraToObject{0.0, 0.0, 0.0};

    [[nodiscard]] math::Vec3f transformPatchVertex(const math::Vec3f& patchLocalVertex) const noexcept;
    [[nodiscard]] math::Vec3f transformPlanetVertex(const math::Vec3d& planetRelativeVertex) const noexcept;
};

[[nodiscard]] LargeObjectSetup makeLargeObjectSetup(
    const WorldPosition& objectCentre, const WorldPosition& camera,
    const math::Vec3d& localOriginOffset = math::Vec3d{0.0, 0.0, 0.0}) noexcept;

} // namespace starforge
