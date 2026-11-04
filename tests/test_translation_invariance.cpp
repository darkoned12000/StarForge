#include <doctest/doctest.h>

#include "spatial/camera_relative.hpp"
#include "spatial/world_position.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

using namespace starforge::math;
using namespace starforge::spatial;

namespace
{

constexpr double kCellSize = WorldPosition::kCellSize;
constexpr double kEpsilon = 1e-9;

bool approxEqual(double a, double b, double eps = kEpsilon)
{
    return std::fabs(a - b) < eps;
}

bool approxEqualVec3d(const Vec3d& a, const Vec3d& b, double eps = kEpsilon)
{
    return approxEqual(a.x, b.x, eps) &&
           approxEqual(a.y, b.y, eps) &&
           approxEqual(a.z, b.z, eps);
}

struct Rng
{
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}

    std::uint64_t nextU64() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }

    double nextDouble(double lo, double hi) noexcept
    {
        // Generate a 53-bit uniform fraction in [0, 1).
        const double t = static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
        return lo + (hi - lo) * t;
    }

    std::int64_t nextCell(std::int64_t lo, std::int64_t hi) noexcept
    {
        const auto span = static_cast<std::uint64_t>(hi - lo) + 1ULL;
        return lo + static_cast<std::int64_t>(nextU64() % span);
    }
};

} // namespace

TEST_CASE("Translation invariance: same relative scene shifted from origin to 1e24 m (near INT64_MAX cells)")
{
    const Vec3d sceneOffsets[] = {
        {0.0, 0.0, 0.0},
        {123.456789, -987.654321, 0.00042},
        {-54321.125, 1048576.5, -2097151.75}, // crosses multiple 2^20 m cell boundaries
        {9999.9999, -0.0009, 42.125},
    };

    auto camOrigin = WorldPosition::fromCellAndOffset(0, 0, 0, 12.345, 678.9, 101112.25);
    REQUIRE(camOrigin.has_value());

    WorldPosition objOrigin[4];
    Vec3d refRelDouble[4];
    Vec3f refRelFloat[4];
    for (int i = 0; i < 4; ++i)
    {
        auto p = camOrigin->translated(sceneOffsets[i]);
        REQUIRE(p.has_value());
        objOrigin[i] = *p;
        refRelDouble[i] = toCameraRelative(objOrigin[i], *camOrigin);
        refRelFloat[i] = toFloat32(refRelDouble[i]);
        CHECK(approxEqualVec3d(refRelDouble[i], sceneOffsets[i], 1e-9));
    }

    // Shift the entire scene across scales: 1e6 m, 1e12 m, 1e15 m, 1e21 m, and ~9.6e24 m (near INT64_MAX).
    const std::int64_t cellShifts[][3] = {
        {1, -1, 2},                                                   // ~1e6 m
        {953674LL, -953674LL, 476837LL},                              // ~1e12 m
        {953674316LL, 476837158LL, -953674316LL},                     // ~1e15 m
        {953674316406250LL, -953674316406250LL, 123456789012345LL},   // ~1e21 m
        {9000000000000000000LL, -9000000000000000000LL, 4500000000000000000LL}, // ~9.4e24 m (near INT64_MAX)
    };

    for (const auto& shift : cellShifts)
    {
        auto camShifted = camOrigin->shiftedByCells(shift[0], shift[1], shift[2]);
        REQUIRE(camShifted.has_value());

        // Also apply a sub-cell shift so cell boundaries are crossed differently at the target origin.
        const Vec3d subCellShift{777777.125, -888888.375, 555555.625};
        auto camShiftedFull = camShifted->translated(subCellShift);
        REQUIRE(camShiftedFull.has_value());

        for (int i = 0; i < 4; ++i)
        {
            auto objShiftedCell = objOrigin[i].shiftedByCells(shift[0], shift[1], shift[2]);
            REQUIRE(objShiftedCell.has_value());
            auto objShiftedFull = objShiftedCell->translated(subCellShift);
            REQUIRE(objShiftedFull.has_value());

            const Vec3d relD = toCameraRelative(*objShiftedFull, *camShiftedFull);
            const Vec3f relF = toFloat32(relD);

            // Sub-nanometre invariance in double, and well within 1 micrometre in float32!
            CHECK(approxEqualVec3d(relD, refRelDouble[i], 1e-9));
            CHECK(std::fabs(relF.x - refRelFloat[i].x) < 1e-6f);
            CHECK(std::fabs(relF.y - refRelFloat[i].y) < 1e-6f);
            CHECK(std::fabs(relF.z - refRelFloat[i].z) < 1e-6f);
        }
    }
}

TEST_CASE("Translation invariance: operator- near opposite extremes without overflow")
{
    // Position near +INT64_MAX cells.
    auto posMax = WorldPosition::fromCellAndOffset(
        std::numeric_limits<std::int64_t>::max() - 10, 0, 0, 500.0, 0.0, 0.0);
    REQUIRE(posMax.has_value());

    // Position near -INT64_MAX cells.
    auto posMin = WorldPosition::fromCellAndOffset(
        std::numeric_limits<std::int64_t>::min() + 10, 0, 0, 500.0, 0.0, 0.0);
    REQUIRE(posMin.has_value());

    // The difference should not invoke UB and should be approximately correct.
    Vec3d diff = *posMax - *posMin;
    CHECK(diff.x > 0.0);
    CHECK(std::isfinite(diff.x));

    // Reverse direction.
    Vec3d diffRev = *posMin - *posMax;
    CHECK(diffRev.x < 0.0);
    CHECK(std::isfinite(diffRev.x));
}

TEST_CASE("Translation invariance: property test with random cells, negative coordinates, and boundary crossings")
{
    Rng rng(0x57A4F046EULL);

    for (int trial = 0; trial < 250; ++trial)
    {
        const std::int64_t baseCx = rng.nextCell(-1000, 1000);
        const std::int64_t baseCy = rng.nextCell(-1000, 1000);
        const std::int64_t baseCz = rng.nextCell(-1000, 1000);
        const double baseOx = rng.nextDouble(0.0, kCellSize);
        const double baseOy = rng.nextDouble(0.0, kCellSize);
        const double baseOz = rng.nextDouble(0.0, kCellSize);

        auto cam0 = WorldPosition::fromCellAndOffset(baseCx, baseCy, baseCz, baseOx, baseOy, baseOz);
        REQUIRE(cam0.has_value());

        // Relative displacement spanning [-5 cells, +5 cells] with sub-millimetre fractions.
        const Vec3d relOffset{
            rng.nextDouble(-5.0 * kCellSize, 5.0 * kCellSize),
            rng.nextDouble(-5.0 * kCellSize, 5.0 * kCellSize),
            rng.nextDouble(-5.0 * kCellSize, 5.0 * kCellSize),
        };

        auto obj0 = cam0->translated(relOffset);
        REQUIRE(obj0.has_value());

        const Vec3d refRel = toCameraRelative(*obj0, *cam0);
        CHECK(approxEqualVec3d(refRel, relOffset, 1e-8));

        // Random shift T across the full +/- 1e18 cell range (up to ~1e24 m).
        const std::int64_t shiftCx = rng.nextCell(-1000000000000000000LL, 1000000000000000000LL);
        const std::int64_t shiftCy = rng.nextCell(-1000000000000000000LL, 1000000000000000000LL);
        const std::int64_t shiftCz = rng.nextCell(-1000000000000000000LL, 1000000000000000000LL);
        const Vec3d shiftSub{
            rng.nextDouble(-kCellSize, kCellSize),
            rng.nextDouble(-kCellSize, kCellSize),
            rng.nextDouble(-kCellSize, kCellSize),
        };

        auto camShift1 = cam0->shiftedByCells(shiftCx, shiftCy, shiftCz);
        auto objShift1 = obj0->shiftedByCells(shiftCx, shiftCy, shiftCz);
        REQUIRE(camShift1.has_value());
        REQUIRE(objShift1.has_value());

        auto camT = camShift1->translated(shiftSub);
        auto objT = objShift1->translated(shiftSub);
        REQUIRE(camT.has_value());
        REQUIRE(objT.has_value());

        const Vec3d shiftedRel = toCameraRelative(*objT, *camT);
        CHECK(approxEqualVec3d(shiftedRel, refRel, 1e-8));
    }
}
