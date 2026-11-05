#include "spatial/world_position.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace starforge::spatial
{

namespace
{

// Safe signed 64-bit addition with overflow detection.
bool checkedAddInt64(std::int64_t a, std::int64_t b, std::int64_t& out) noexcept
{
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
    {
        return false;
    }
    out = a + b;
    return true;
}

// Normalises one (cell, offset) pair so that offset is in [0, kCellSize),
// updating `cell` with overflow checking.
bool normalizeAxisChecked(std::int64_t& cell, double& offset) noexcept
{
    if (!std::isfinite(offset))
    {
        return false;
    }

    const double cellDeltaFloor = std::floor(offset / WorldPosition::kCellSize);
    // static_cast<double>(INT64_MAX) rounds up to +2^63 (9223372036854775808.0),
    // which is 1 greater than INT64_MAX. Require strictly less than +2^63 and
    // >= -2^63.
    if (cellDeltaFloor < WorldPosition::kMinInt64AsDouble || cellDeltaFloor >= WorldPosition::kMaxInt64BoundExclusive)
    {
        return false;
    }

    const auto cellDelta = static_cast<std::int64_t>(cellDeltaFloor);
    if (!checkedAddInt64(cell, cellDelta, cell))
    {
        return false;
    }

    offset -= cellDeltaFloor * WorldPosition::kCellSize;

    // Guard against floating-point rounding drift near cell boundaries.
    if (offset < 0.0)
    {
        offset += WorldPosition::kCellSize;
        if (!checkedAddInt64(cell, -1, cell))
        {
            return false;
        }
    }
    if (offset >= WorldPosition::kCellSize)
    {
        offset -= WorldPosition::kCellSize;
        if (!checkedAddInt64(cell, 1, cell))
        {
            return false;
        }
    }

    // Normalize -0.0 to +0.0 for deterministic equality checks.
    if (offset == 0.0)
    {
        offset = 0.0;
    }

    return true;
}

std::int64_t arithmeticShiftRight(std::int64_t value, int shiftBits) noexcept
{
    if (shiftBits <= 0)
    {
        return value;
    }
    if (shiftBits >= 63)
    {
        return value < 0 ? -1 : 0;
    }
    return value >> shiftBits;
}

} // namespace

std::expected<WorldPosition, std::string>
WorldPosition::fromCellAndOffset(std::int64_t cx, std::int64_t cy, std::int64_t cz,
                                 double ox, double oy, double oz)
{
    if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz))
    {
        return std::unexpected("WorldPosition: NaN or infinity not allowed");
    }

    WorldPosition pos;
    pos.cellX = cx;
    pos.cellY = cy;
    pos.cellZ = cz;
    pos.offsetX = ox;
    pos.offsetY = oy;
    pos.offsetZ = oz;

    if (!normalizeAxisChecked(pos.cellX, pos.offsetX) ||
        !normalizeAxisChecked(pos.cellY, pos.offsetY) ||
        !normalizeAxisChecked(pos.cellZ, pos.offsetZ))
    {
        return std::unexpected("WorldPosition: coordinate exceeds maximum representable range");
    }

    return pos;
}

std::expected<WorldPosition, std::string> WorldPosition::fromMeters(double x, double y, double z)
{
    return fromCellAndOffset(0, 0, 0, x, y, z);
}

std::expected<WorldPosition, std::string> WorldPosition::fromMeters(const math::Vec3d& pos)
{
    return fromMeters(pos.x, pos.y, pos.z);
}

math::Vec3d WorldPosition::toMeters() const noexcept
{
    return math::Vec3d{
        static_cast<double>(cellX) * kCellSize + offsetX,
        static_cast<double>(cellY) * kCellSize + offsetY,
        static_cast<double>(cellZ) * kCellSize + offsetZ,
    };
}

std::expected<WorldPosition, std::string>
WorldPosition::shiftedByCells(std::int64_t dCellX, std::int64_t dCellY, std::int64_t dCellZ) const
{
    WorldPosition out = *this;
    if (!checkedAddInt64(out.cellX, dCellX, out.cellX) ||
        !checkedAddInt64(out.cellY, dCellY, out.cellY) ||
        !checkedAddInt64(out.cellZ, dCellZ, out.cellZ))
    {
        return std::unexpected("WorldPosition::shiftedByCells: int64 cell overflow");
    }
    return out;
}

std::expected<WorldPosition, std::string>
WorldPosition::translated(const math::Vec3d& delta) const
{
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
    {
        return std::unexpected("WorldPosition::translated: NaN or infinity not allowed");
    }

    // Split each axis into whole cells via trunc(delta / kCellSize) and a signed
    // remainder in (-kCellSize, kCellSize). This avoids converting negative small
    // deltas into ~kCellSize offsets (which would lose sub-nanometre precision).
    auto splitAxis = [](double d, std::int64_t& cellDelta, double& rem) noexcept -> bool {
        const double q = std::trunc(d / kCellSize);
        if (q < WorldPosition::kMinInt64AsDouble || q >= WorldPosition::kMaxInt64BoundExclusive)
        {
            return false;
        }
        cellDelta = static_cast<std::int64_t>(q);
        rem = d - q * kCellSize;
        return true;
    };

    std::int64_t dcx = 0, dcy = 0, dcz = 0;
    double rx = 0.0, ry = 0.0, rz = 0.0;
    if (!splitAxis(delta.x, dcx, rx) ||
        !splitAxis(delta.y, dcy, ry) ||
        !splitAxis(delta.z, dcz, rz))
    {
        return std::unexpected("WorldPosition::translated: displacement exceeds range");
    }

    std::int64_t cx = 0, cy = 0, cz = 0;
    if (!checkedAddInt64(cellX, dcx, cx) ||
        !checkedAddInt64(cellY, dcy, cy) ||
        !checkedAddInt64(cellZ, dcz, cz))
    {
        return std::unexpected("WorldPosition::translated: int64 cell overflow");
    }

    return fromCellAndOffset(cx, cy, cz, offsetX + rx, offsetY + ry, offsetZ + rz);
}

WorldPosition& WorldPosition::operator+=(const math::Vec3d& delta)
{
    if (auto moved = translated(delta))
    {
        *this = *moved;
    }
    else
    {
        // Log the failure; caller should use translated() for explicit error handling.
        // In debug builds this is a programming error.
#ifndef NDEBUG
        assert(false && "WorldPosition translation overflow or invalid delta");
#endif
    }
    return *this;
}

WorldPosition& WorldPosition::operator-=(const math::Vec3d& delta)
{
    return *this += math::Vec3d{-delta.x, -delta.y, -delta.z};
}

WorldPosition WorldPosition::operator+(const math::Vec3d& delta) const
{
    WorldPosition result = *this;
    result += delta;
    return result;
}

WorldPosition operator+(const math::Vec3d& delta, const WorldPosition& pos)
{
    return pos + delta;
}

WorldPosition WorldPosition::operator-(const math::Vec3d& delta) const
{
    WorldPosition result = *this;
    result -= delta;
    return result;
}

math::Vec3d WorldPosition::operator-(const WorldPosition& other) const noexcept
{
    // Compute the relative vector using cell differences and offset differences
    // directly, avoiding the catastrophic cancellation of converting both
    // positions to absolute metres. Promote to double when the int64 subtraction
    // would overflow to avoid undefined behaviour.
    auto safeDelta = [](std::int64_t c1, double o1, std::int64_t c2, double o2) noexcept -> double {
        // If the subtraction would overflow int64, promote to double.
        if ((c2 > 0 && c1 < std::numeric_limits<std::int64_t>::min() + c2) ||
            (c2 < 0 && c1 > std::numeric_limits<std::int64_t>::max() + c2))
        {
            return (static_cast<double>(c1) - static_cast<double>(c2)) * kCellSize + (o1 - o2);
        }
        return static_cast<double>(c1 - c2) * kCellSize + (o1 - o2);
    };

    return math::Vec3d{
        safeDelta(cellX, offsetX, other.cellX, other.offsetX),
        safeDelta(cellY, offsetY, other.cellY, other.offsetY),
        safeDelta(cellZ, offsetZ, other.cellZ, other.offsetZ),
    };
}

bool WorldPosition::operator==(const WorldPosition& other) const noexcept
{
    return cellX == other.cellX && cellY == other.cellY && cellZ == other.cellZ &&
           offsetX == other.offsetX && offsetY == other.offsetY && offsetZ == other.offsetZ;
}

bool WorldPosition::operator!=(const WorldPosition& other) const noexcept
{
    return !(*this == other);
}

std::uint64_t WorldPosition::cellHash() const noexcept
{
    // FNV-1a 64-bit hash of cell coordinates (little-endian byte order).
    // Strictly integer-only: floating-point offsets never feed the hash.
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    std::uint64_t hash = kOffsetBasis;

    auto mixInt64 = [&hash](std::int64_t value) noexcept {
        auto v = static_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i)
        {
            hash ^= (v & 0xFFULL);
            hash *= kPrime;
            v >>= 8;
        }
    };

    mixInt64(cellX);
    mixInt64(cellY);
    mixInt64(cellZ);

    return hash;
}

std::expected<bool, std::string> WorldPosition::normalize() noexcept
{
    std::int64_t cx = cellX, cy = cellY, cz = cellZ;
    double ox = offsetX, oy = offsetY, oz = offsetZ;

    bool changedX = false, changedY = false, changedZ = false;

    if (ox < 0.0 || ox >= kCellSize || !std::isfinite(ox) || (ox == 0.0 && std::signbit(ox)))
    {
        if (!normalizeAxisChecked(cx, ox))
        {
            return std::unexpected("WorldPosition::normalize failed on X");
        }
        changedX = true;
    }
    if (oy < 0.0 || oy >= kCellSize || !std::isfinite(oy) || (oy == 0.0 && std::signbit(oy)))
    {
        if (!normalizeAxisChecked(cy, oy))
        {
            return std::unexpected("WorldPosition::normalize failed on Y");
        }
        changedY = true;
    }
    if (oz < 0.0 || oz >= kCellSize || !std::isfinite(oz) || (oz == 0.0 && std::signbit(oz)))
    {
        if (!normalizeAxisChecked(cz, oz))
        {
            return std::unexpected("WorldPosition::normalize failed on Z");
        }
        changedZ = true;
    }

    if (changedX || changedY || changedZ)
    {
        cellX = cx;
        cellY = cy;
        cellZ = cz;
        offsetX = ox;
        offsetY = oy;
        offsetZ = oz;
        return true;
    }
    return false;
}

bool WorldPosition::isNormalized() const noexcept
{
    return offsetX >= 0.0 && offsetX < kCellSize &&
           offsetY >= 0.0 && offsetY < kCellSize &&
           offsetZ >= 0.0 && offsetZ < kCellSize;
}

std::int64_t WorldPosition::coarseCellByBitsX(int shiftBits) const noexcept
{
    return arithmeticShiftRight(cellX, shiftBits);
}

std::int64_t WorldPosition::coarseCellByBitsY(int shiftBits) const noexcept
{
    return arithmeticShiftRight(cellY, shiftBits);
}

std::int64_t WorldPosition::coarseCellByBitsZ(int shiftBits) const noexcept
{
    return arithmeticShiftRight(cellZ, shiftBits);
}

std::int64_t WorldPosition::coarseCellX(int level) const noexcept
{
    if (level <= 0) return cellX;
    if (level >= 4) return cellX < 0 ? -1 : 0;
    return arithmeticShiftRight(cellX, level * kCellBits);
}

std::int64_t WorldPosition::coarseCellY(int level) const noexcept
{
    if (level <= 0) return cellY;
    if (level >= 4) return cellY < 0 ? -1 : 0;
    return arithmeticShiftRight(cellY, level * kCellBits);
}

std::int64_t WorldPosition::coarseCellZ(int level) const noexcept
{
    if (level <= 0) return cellZ;
    if (level >= 4) return cellZ < 0 ? -1 : 0;
    return arithmeticShiftRight(cellZ, level * kCellBits);
}

} // namespace starforge::spatial
