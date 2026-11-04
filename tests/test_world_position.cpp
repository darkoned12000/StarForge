#include <doctest/doctest.h>

#include "spatial/world_position.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace math = starforge::math;
using namespace starforge::spatial;

namespace
{

constexpr double kCellSize = WorldPosition::kCellSize;
constexpr double kEpsilon = 1e-9;

bool approxEqual(double a, double b, double eps = kEpsilon)
{
    return std::fabs(a - b) < eps;
}

} // namespace

TEST_CASE("WorldPosition: round-trip at 1e12 m")
{
    auto pos = WorldPosition::fromMeters(1e12, 2e12, 3e12);
    REQUIRE(pos.has_value());

    math::Vec3d meters = pos->toMeters();
    CHECK(approxEqual(meters.x, 1e12));
    CHECK(approxEqual(meters.y, 2e12));
    CHECK(approxEqual(meters.z, 3e12));

    auto roundTrip = WorldPosition::fromMeters(meters);
    REQUIRE(roundTrip.has_value());
    CHECK(*roundTrip == *pos);
}

TEST_CASE("WorldPosition: explicit cell+offset construction and precision at 1e24 m")
{
    // ~1e24 m is ~9.536e17 cells of size 2^20 m.
    constexpr std::int64_t farCell = 953674316406250000LL;
    auto p1 = WorldPosition::fromCellAndOffset(farCell, -farCell, 42, 123.456789, 0.00025, 999.5);
    REQUIRE(p1.has_value());
    CHECK(p1->cellX == farCell);
    CHECK(p1->cellY == -farCell);
    CHECK(p1->cellZ == 42);
    CHECK(approxEqual(p1->offsetX, 123.456789, 1e-12));
    CHECK(approxEqual(p1->offsetY, 0.00025, 1e-12));
    CHECK(approxEqual(p1->offsetZ, 999.5, 1e-12));

    // Adding a 0.5 mm delta at 1e24 m preserves sub-millimetre precision!
    WorldPosition p2 = *p1 + math::Vec3d{0.0005, -0.0001, 0.0};
    math::Vec3d diff = p2 - *p1;
    CHECK(approxEqual(diff.x, 0.0005, 1e-12));
    CHECK(approxEqual(diff.y, -0.0001, 1e-12));
    CHECK(approxEqual(diff.z, 0.0, 1e-12));
}

TEST_CASE("WorldPosition: negative coordinates with floor semantics")
{
    auto pos = WorldPosition::fromMeters(-1.0, -1.0, -1.0);
    REQUIRE(pos.has_value());

    CHECK(pos->cellX == -1);
    CHECK(pos->cellY == -1);
    CHECK(pos->cellZ == -1);
    CHECK(approxEqual(pos->offsetX, kCellSize - 1.0));
    CHECK(approxEqual(pos->offsetY, kCellSize - 1.0));
    CHECK(approxEqual(pos->offsetZ, kCellSize - 1.0));

    math::Vec3d meters = pos->toMeters();
    CHECK(approxEqual(meters.x, -1.0));
    CHECK(approxEqual(meters.y, -1.0));
    CHECK(approxEqual(meters.z, -1.0));
}

TEST_CASE("WorldPosition: cell boundaries")
{
    double boundary = kCellSize * 3.0;
    auto pos = WorldPosition::fromMeters(boundary, 0.0, 0.0);
    REQUIRE(pos.has_value());
    CHECK(pos->cellX == 3);
    CHECK(pos->offsetX == 0.0);

    auto pos2 = WorldPosition::fromMeters(boundary - 1.0, 0.0, 0.0);
    REQUIRE(pos2.has_value());
    CHECK(pos2->cellX == 2);
    CHECK(approxEqual(pos2->offsetX, kCellSize - 1.0));

    auto pos3 = WorldPosition::fromMeters(boundary + 1.0, 0.0, 0.0);
    REQUIRE(pos3.has_value());
    CHECK(pos3->cellX == 3);
    CHECK(approxEqual(pos3->offsetX, 1.0));
}

TEST_CASE("WorldPosition: NaN and infinity rejected")
{
    CHECK_FALSE(WorldPosition::fromMeters(std::nan(""), 0.0, 0.0).has_value());
    CHECK_FALSE(WorldPosition::fromMeters(0.0, std::numeric_limits<double>::infinity(), 0.0).has_value());
    CHECK_FALSE(WorldPosition::fromMeters(0.0, 0.0, -std::numeric_limits<double>::infinity()).has_value());
    CHECK_FALSE(WorldPosition::fromCellAndOffset(0, 0, 0, std::nan(""), 0.0, 0.0).has_value());
}

TEST_CASE("WorldPosition: overflow detection")
{
    // 2^63 * 2^20 overflows int64 cell index and must be rejected without UB.
    const double exactOverflowBoundary = 9223372036854775808.0 * kCellSize;
    CHECK_FALSE(WorldPosition::fromMeters(exactOverflowBoundary, 0.0, 0.0).has_value());
    CHECK_FALSE(WorldPosition::fromMeters(0.0, exactOverflowBoundary, 0.0).has_value());

    // Shifting beyond INT64_MAX / INT64_MIN is rejected.
    auto atMax = WorldPosition::fromCellAndOffset(std::numeric_limits<std::int64_t>::max(), 0, 0, 10.0, 0.0, 0.0);
    REQUIRE(atMax.has_value());
    CHECK_FALSE(atMax->shiftedByCells(1, 0, 0).has_value());
    CHECK_FALSE(atMax->translated(math::Vec3d{kCellSize, 0.0, 0.0}).has_value());
}

TEST_CASE("WorldPosition: known cells hash to known FNV-1a 64-bit values")
{
    auto origin = WorldPosition::fromCellAndOffset(0, 0, 0, 123.0, 456.0, 789.0);
    auto originZero = WorldPosition::fromCellAndOffset(0, 0, 0, 0.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(originZero.has_value());

    // Hash depends strictly on cell coordinates, never on floating-point offsets.
    CHECK(origin->cellHash() == originZero->cellHash());
    CHECK(origin->cellHash() == 9354609568656401157ULL);

    auto c123 = WorldPosition::fromCellAndOffset(1, 2, 3);
    REQUIRE(c123.has_value());
    CHECK(c123->cellHash() == 15720935049292226309ULL);

    auto neg1 = WorldPosition::fromCellAndOffset(-1, -1, -1);
    REQUIRE(neg1.has_value());
    CHECK(neg1->cellHash() == 5557926104951861613ULL);
}

TEST_CASE("WorldPosition: arithmetic preserves sub-mm precision across cell boundaries at 1e16 m")
{
    // At 1e16 m (~9.536e9 cells), double spacing is 2 m. Verify that adding
    // a delta that crosses a cell boundary still preserves sub-millimetre accuracy.
    constexpr std::int64_t cell1e16 = 9536743164LL;
    auto pos = WorldPosition::fromCellAndOffset(cell1e16, 0, 0, kCellSize - 10.0, 0.0, 0.0);
    REQUIRE(pos.has_value());

    WorldPosition moved = *pos + math::Vec3d{25.00075, 0.0, 0.0};
    CHECK(moved.cellX == cell1e16 + 1);
    CHECK(approxEqual(moved.offsetX, 15.00075, 1e-9));

    math::Vec3d delta = moved - *pos;
    CHECK(approxEqual(delta.x, 25.00075, 1e-9));
}

TEST_CASE("WorldPosition: shift-based coarse cell index with floor semantics")
{
    auto pos = WorldPosition::fromCellAndOffset((3LL << 20) + 17, -(1LL << 20) - 1, -1);
    REQUIRE(pos.has_value());

    CHECK(pos->coarseCellX(0) == (3LL << 20) + 17);
    CHECK(pos->coarseCellX(1) == 3);
    CHECK(pos->coarseCellY(1) == -2); // floor((-(2^20) - 1) / 2^20) == -2
    CHECK(pos->coarseCellZ(1) == -1); // floor(-1 / 2^20) == -1

    // Arbitrary power-of-two bit shift also uses floor semantics.
    CHECK(pos->coarseCellByBitsZ(4) == -1);
}
