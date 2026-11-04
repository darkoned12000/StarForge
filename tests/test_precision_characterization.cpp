#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>

// M2.1: Characterization tool for position representations.
// Reach and spacing of float, double, int64 millimetres, and int64 cell +
// double offset, from 1e6 m to beyond 1e24 m. Uses no math library.

namespace
{

constexpr double kCellSize = 1048576.0; // 2^20 m
constexpr double kInt64MmMaxReach = 9.2233720368547748e15; // < 2^63 mm in metres
constexpr double kCellOffsetMaxReach = 9.2233720368547748e18 * kCellSize; // < 2^63 cells * 2^20 m

double floatSpacing(double d) noexcept
{
    float f = static_cast<float>(d);
    if (!std::isfinite(f))
    {
        return std::numeric_limits<double>::infinity();
    }
    float next = std::nextafter(f, std::numeric_limits<float>::infinity());
    return static_cast<double>(next) - static_cast<double>(f);
}

double doubleSpacing(double d) noexcept
{
    double next = std::nextafter(d, std::numeric_limits<double>::infinity());
    return next - d;
}

double cellOffsetWorstCaseSpacing() noexcept
{
    // The offset within a cell is a double in [0, 2^20). The worst-case spacing
    // between adjacent representable doubles in [0, 2^20) occurs in [2^19, 2^20),
    // where ULP = 2^19 * 2^-52 = 2^-33 ≈ 1.164e-10 m (half-spacing error ≈ 5.82e-11 m).
    // This is invariant of absolute distance because the int64 cell index carries
    // the coarse position exactly.
    const double offset = kCellSize * 0.5;
    const double nextOffset = std::nextafter(offset, std::numeric_limits<double>::infinity());
    return nextOffset - offset;
}

struct CharRow
{
    double distance;
    double floatErr;      // half of float spacing (metres)
    double doubleErr;     // half of double spacing (metres)
    double int64MmErr;    // half of 1 mm (metres), or NaN if out of range
    double cellOffsetErr; // half of worst-case cell+offset spacing (metres), or NaN if out of range
};

CharRow characterize(double d) noexcept
{
    CharRow row{};
    row.distance = d;
    row.floatErr = floatSpacing(d) * 0.5;
    row.doubleErr = doubleSpacing(d) * 0.5;
    row.int64MmErr = (d <= kInt64MmMaxReach) ? (0.001 * 0.5) : std::numeric_limits<double>::quiet_NaN();
    row.cellOffsetErr = (d <= kCellOffsetMaxReach) ? (cellOffsetWorstCaseSpacing() * 0.5) : std::numeric_limits<double>::quiet_NaN();
    return row;
}

} // namespace

TEST_CASE("M2.1: position representation characterization table matches reference values")
{
    // Verify the exact numbers recorded in docs/decisions/001-world-scale-requirements.md.
    const CharRow r1e6 = characterize(1e6);
    CHECK(r1e6.floatErr == doctest::Approx(0.03125).epsilon(1e-3));
    CHECK(r1e6.doubleErr == doctest::Approx(5.82077e-11).epsilon(1e-3));
    CHECK(r1e6.int64MmErr == doctest::Approx(0.0005));
    CHECK(r1e6.cellOffsetErr == doctest::Approx(5.82077e-11).epsilon(1e-3));

    const CharRow r1e12 = characterize(1e12);
    CHECK(r1e12.floatErr == doctest::Approx(32768.0).epsilon(1e-3));
    CHECK(r1e12.doubleErr == doctest::Approx(6.10352e-05).epsilon(1e-3));
    CHECK(r1e12.cellOffsetErr == doctest::Approx(5.82077e-11).epsilon(1e-3));

    const CharRow r1e15 = characterize(1e15);
    CHECK(r1e15.doubleErr == doctest::Approx(0.0625).epsilon(1e-3));
    CHECK(r1e15.cellOffsetErr == doctest::Approx(5.82077e-11).epsilon(1e-3));
}

TEST_CASE("M2.1: naive float error exceeds stated bound at each distance")
{
    CHECK(characterize(1e6).floatErr > 0.01);
    CHECK(characterize(1e9).floatErr > 10.0);
    CHECK(characterize(1e12).floatErr > 10000.0);

    const double testDists[] = {1e6, 1e9, 1e12, 1e15};
    for (double d : testDists)
    {
        CHECK(characterize(d).floatErr > 0.001);
    }
}

TEST_CASE("M2.1: cell+offset achieves sub-millimetre at all tested distances up to 1e24 m")
{
    const double distances[] = {1e6, 1e9, 1e12, 4.5e12, 1e13, 1e15, 1e16, 1e21, 1e24};

    for (double d : distances)
    {
        const CharRow row = characterize(d);
        REQUIRE_FALSE(std::isnan(row.cellOffsetErr));
        CHECK(row.cellOffsetErr < 1e-9); // < 1 nanometre (well under 1 mm)
    }
}

TEST_CASE("M2.1: int64 millimetres goes out of range beyond ~9.22e15 m")
{
    CHECK_FALSE(std::isnan(characterize(1e15).int64MmErr));
    CHECK(std::isnan(characterize(1e16).int64MmErr));
}

TEST_CASE("M2.1: double precision fails sub-mm at required extent (1e15 m)")
{
    const CharRow row = characterize(1e15);
    CHECK(row.doubleErr > 0.01);
}

TEST_CASE("M2.1: cell+offset reaches 1e24 m and rejects 1e25 m")
{
    CHECK_FALSE(std::isnan(characterize(1e24).cellOffsetErr));
    CHECK(std::isnan(characterize(1e25).cellOffsetErr));
}
