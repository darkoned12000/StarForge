#pragma once

#include "math/math.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace starforge::spatial
{

// A world position stored as an int64 cell index (power-of-two cell size)
// plus a double local offset within the cell. This gives exact integer
// addressing at any scale (deterministic generation) while maintaining
// sub-millimetre local precision regardless of distance from the origin.
//
// Cell size is 2^20 m (~1.05 million metres). The int64 cell index gives a
// reach of ~9.7e24 m (interstellar). The double offset in [0, 2^20) gives
// worst-case spacing 2^-33 m (~0.116 nm) and half-spacing error 2^-34 m
// (~0.058 nm) within one cell.
struct WorldPosition
{
    static constexpr int kCellBits = 20;
    static constexpr double kCellSize = static_cast<double>(1LL << kCellBits); // 2^20 m = 1,048,576 m
    // Largest finite double value whose floor(x / kCellSize) fits in int64_t.
    // Note: static_cast<double>(INT64_MAX) rounds up to 2^63, so we use the
    // predecessor float64 value below 2^63 * 2^20.
    static constexpr double kMaxReach = 9.2233720368547748e18 * kCellSize;

    // Bounds used by normalizeAxisChecked (avoid magic numbers).
    static constexpr double kMinInt64AsDouble = -9223372036854775808.0;       // -2^63
    static constexpr double kMaxInt64BoundExclusive = 9223372036854775808.0;  // +2^63

    std::int64_t cellX = 0;
    std::int64_t cellY = 0;
    std::int64_t cellZ = 0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double offsetZ = 0.0;

    // Construction from explicit cell coordinates and local offset (in metres).
    // Normalises offsets outside [0, kCellSize) into the cell coordinates and
    // returns an error on NaN/infinity or int64 cell overflow.
    [[nodiscard]] static std::expected<WorldPosition, std::string>
    fromCellAndOffset(std::int64_t cx, std::int64_t cy, std::int64_t cz,
                      double ox = 0.0, double oy = 0.0, double oz = 0.0);

    // Construction from metres (double). Returns an error on NaN/inf or when
    // the value exceeds the representable int64 cell range.
    [[nodiscard]] static std::expected<WorldPosition, std::string>
    fromMeters(double x, double y, double z);

    [[nodiscard]] static std::expected<WorldPosition, std::string>
    fromMeters(const math::Vec3d& pos);

    // Convert back to metres (double). Note: a single double loses sub-mm
    // precision beyond ~1e13 m; use toCameraRelative() or operator- for
    // precision-preserving relative math.
    [[nodiscard]] math::Vec3d toMeters() const noexcept;

    // Checked shift by an exact number of cells along each axis. Preserves
    // the local offset bit-for-bit and returns an error on int64 overflow.
    [[nodiscard]] std::expected<WorldPosition, std::string>
    shiftedByCells(std::int64_t dCellX, std::int64_t dCellY, std::int64_t dCellZ) const;

    // Checked translation by a displacement in metres. Splits the displacement
    // into cells and local offset before adding so sub-millimetre precision is
    // preserved at any distance from the world origin.
    [[nodiscard]] std::expected<WorldPosition, std::string>
    translated(const math::Vec3d& delta) const;

    // Arithmetic: add/subtract a local offset (metres) directly on cell+offset
    // without round-tripping through toMeters().
    WorldPosition& operator+=(const math::Vec3d& delta);
    WorldPosition& operator-=(const math::Vec3d& delta);
    [[nodiscard]] WorldPosition operator+(const math::Vec3d& delta) const;
    [[nodiscard]] WorldPosition operator-(const math::Vec3d& delta) const;

    // Commutative addition: Vec3d + WorldPosition
    friend WorldPosition operator+(const math::Vec3d& delta, const WorldPosition& pos);

    // Relative vector between two positions (in metres), computed from cell
    // differences and offset differences directly.
    [[nodiscard]] math::Vec3d operator-(const WorldPosition& other) const noexcept;

    // Equality
    [[nodiscard]] bool operator==(const WorldPosition& other) const noexcept;
    [[nodiscard]] bool operator!=(const WorldPosition& other) const noexcept;

    // Stable 64-bit hash of the cell coordinates (FNV-1a, little-endian bytes).
    // Pure integer path — no floating-point values feed the hash.
    [[nodiscard]] std::uint64_t cellHash() const noexcept;

    // Normalize the offset into [0, kCellSize) by adjusting cell coordinates.
    // Returns true if normalization was needed, or an error on NaN/inf/overflow.
    [[nodiscard]] std::expected<bool, std::string> normalize() noexcept;

    // Check whether the offset is in [0, kCellSize).
    [[nodiscard]] bool isNormalized() const noexcept;

    // Coarse cell index at a given scale level using pure integer arithmetic
    // (floor semantics via arithmetic right shift). Each level coarsens by
    // kCellBits (20 bits): level 0 = cell, level 1 = cell >> 20,
    // level 2 = cell >> 40, level >= 3 = cell >> 60 (or sign extension).
    [[nodiscard]] std::int64_t coarseCellX(int level) const noexcept;
    [[nodiscard]] std::int64_t coarseCellY(int level) const noexcept;
    [[nodiscard]] std::int64_t coarseCellZ(int level) const noexcept;

    // Coarse cell index shifted by an arbitrary power-of-two bit count
    // (0..62 bits) with floor semantics.
    [[nodiscard]] std::int64_t coarseCellByBitsX(int shiftBits) const noexcept;
    [[nodiscard]] std::int64_t coarseCellByBitsY(int shiftBits) const noexcept;
    [[nodiscard]] std::int64_t coarseCellByBitsZ(int shiftBits) const noexcept;
};

} // namespace starforge::spatial

// std::hash specialization for WorldPosition (hashes cell + quantized offset).
namespace std
{
template <>
struct hash<starforge::spatial::WorldPosition>
{
    [[nodiscard]] std::size_t operator()(const starforge::spatial::WorldPosition& pos) const noexcept
    {
        std::uint64_t h = pos.cellHash();
        auto hashDouble = [&h](double v) {
            std::uint64_t bits = std::bit_cast<std::uint64_t>(v);
            // SplitMix64-style mixer for better avalanche on integer sequences.
            bits += 0x9e3779b97f4a7c15ULL;
            bits = (bits ^ (bits >> 30)) * 0xbf58476d1ce4e5b9ULL;
            bits = (bits ^ (bits >> 27)) * 0x94d049bb133111b9ULL;
            h ^= bits + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        hashDouble(pos.offsetX);
        hashDouble(pos.offsetY);
        hashDouble(pos.offsetZ);
        return static_cast<std::size_t>(h);
    }
};
} // namespace std
