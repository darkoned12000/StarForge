#include <doctest/doctest.h>

#include "spatial/render_object.hpp"
#include "spatial/world_position.hpp"
#include "spatial/camera_relative.hpp"
#include "math/math.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

using namespace starforge::math;
using namespace starforge::spatial;

namespace
{

// ULP distance between two floats (for signed values and zero).
// Returns the number of representable floats between a and b.
int ulpDistance(float a, float b)
{
    if (a == b) return 0;
    if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<int>::max();
    if (std::isinf(a) || std::isinf(b)) return std::numeric_limits<int>::max();

    auto toBits = [](float f) -> std::int32_t {
        std::uint32_t u;
        std::memcpy(&u, &f, sizeof(u));
        // Convert to signed for proper ordering.
        if (u & 0x80000000u)
        {
            return static_cast<std::int32_t>(0x80000000u - u);
        }
        return static_cast<std::int32_t>(u + 0x80000000u);
    };

    std::int32_t ia = toBits(a);
    std::int32_t ib = toBits(b);
    if (ia < 0 && ib < 0)
    {
        return static_cast<int>(static_cast<std::int64_t>(ib) - static_cast<std::int64_t>(ia));
    }
    return static_cast<int>(static_cast<std::int64_t>(ia) - static_cast<std::int64_t>(ib));
}

// Absolute ULP distance (always non-negative).
int ulpDistanceAbs(float a, float b)
{
    return std::abs(ulpDistance(a, b));
}

bool matricesWithinUlp(const Mat4f& a, const Mat4f& b, int maxUlp = 1)
{
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (ulpDistanceAbs(a[col][row], b[col][row]) > maxUlp)
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("Invariance T1: float matrices bit-identical at origin and 1e12 m")
{
    // Same relative arrangement at two different astronomical origins.
    // Object is 100 m from camera in each axis.
    // Use cell+offset construction so positions at astronomical distances
    // retain sub-cell precision (fromMeters loses precision beyond ~1e13 m).
    const std::int64_t cellShifts[] = {0, 953674316406LL, 953674316406250LL, 9000000000000000000LL};
    const double subCellOffsets[][3] = {
        {0.0, 0.0, 0.0},
        {123.456, 789.012, -345.678},
        {123.456, 789.012, -345.678},
        {123.456, 789.012, -345.678},
    };

    Mat4f referenceMatrix{};

    for (int oi = 0; oi < 4; ++oi)
    {
        auto camPos = WorldPosition::fromCellAndOffset(cellShifts[oi], cellShifts[oi], -cellShifts[oi],
                                                       subCellOffsets[oi][0], subCellOffsets[oi][1], subCellOffsets[oi][2]);
        auto objPos = camPos->translated(Vec3d{100.0, 50.0, -30.0});
        REQUIRE(camPos.has_value());
        REQUIRE(objPos.has_value());

        RenderObject obj;
        obj.position = *objPos;
        obj.localRotationScale = Mat3d(1.0); // identity

        Mat4f model = obj.cameraRelativeModelMatrix(*camPos);

        if (oi == 0)
        {
            referenceMatrix = model;
        }
        else
        {
            // Float matrices must be bit-identical (0 ULP) at all origins.
            CHECK(matricesWithinUlp(model, referenceMatrix, 0));
        }
    }
}

TEST_CASE("Invariance T1: float matrices bit-identical with rotation at 1e12 m")
{
    const double offsets[] = {0.0, 1e6, 1e9, 1e12};

    Mat4f referenceMatrix{};

    // 45-degree rotation around Y axis.
    Mat3d rot(0.0);
    const float c = 0.70710678f;
    const float s = 0.70710678f;
    rot[0] = Vec3d{static_cast<double>(c), 0.0, static_cast<double>(s)};
    rot[1] = Vec3d{0.0, 1.0, 0.0};
    rot[2] = Vec3d{static_cast<double>(-s), 0.0, static_cast<double>(c)};

    for (int oi = 0; oi < 4; ++oi)
    {
        double T = offsets[oi];
        auto camPos = WorldPosition::fromMeters(T, 0.0, 0.0);
        auto objPos = WorldPosition::fromMeters(T + 200.0, 100.0, -50.0);
        REQUIRE(camPos.has_value());
        REQUIRE(objPos.has_value());

        RenderObject obj;
        obj.position = *objPos;
        obj.localRotationScale = rot;

        Mat4f model = obj.cameraRelativeModelMatrix(*camPos);

        if (oi == 0)
        {
            referenceMatrix = model;
        }
        else
        {
            CHECK(matricesWithinUlp(model, referenceMatrix, 0));
        }
    }
}

TEST_CASE("Invariance T1: float matrices bit-identical for multiple objects at several distances")
{
    // Object at various distances from camera.
    const Vec3d offsets[] = {
        {10.0, 5.0, -3.0},
        {1000.0, 500.0, -300.0},
        {10000.0, 5000.0, -3000.0},
    };

    const double origins[] = {0.0, 1e6, 1e9, 1e12};

    for (const Vec3d& relOffset : offsets)
    {
        Mat4f referenceMatrix{};

        for (int oi = 0; oi < 4; ++oi)
        {
            double T = origins[oi];
            auto camPos = WorldPosition::fromMeters(T, T * 0.5, -T * 0.3);
            auto objPos = camPos->translated(relOffset);
            REQUIRE(camPos.has_value());
            REQUIRE(objPos.has_value());

            RenderObject obj;
            obj.position = *objPos;
            obj.localRotationScale = Mat3d(1.0);

            Mat4f model = obj.cameraRelativeModelMatrix(*camPos);

            if (oi == 0)
            {
                referenceMatrix = model;
            }
            else
            {
                CHECK(matricesWithinUlp(model, referenceMatrix, 0));
            }
        }
    }
}

TEST_CASE("Invariance T1: float matrices bit-identical for planet-near-surface case")
{
    // Planet at 1e12 m, camera near surface (radius 6.4e6 m).
    const double planetX = 1e12;
    const double planetRadius = 6.4e6;

    const double origins[] = {0.0, 1e6, 1e9, 1e12};

    Mat4f referenceMatrix{};

    for (int oi = 0; oi < 4; ++oi)
    {
        double T = origins[oi];
        auto planetCentre = WorldPosition::fromMeters(planetX + T, 0.0, 0.0);
        auto camera = WorldPosition::fromMeters(planetX + T, planetRadius + 50.0, 0.0);
        REQUIRE(planetCentre.has_value());
        REQUIRE(camera.has_value());

        RenderObject planet;
        planet.position = *planetCentre;
        planet.localRotationScale = Mat3d(1.0);

        Mat4f model = planet.cameraRelativeModelMatrix(*camera);

        if (oi == 0)
        {
            referenceMatrix = model;
        }
        else
        {
            // The camera-relative origin should be identical regardless of
            // the absolute origin.
            CHECK(matricesWithinUlp(model, referenceMatrix, 0));
        }
    }
}

TEST_CASE("Invariance T1: naive float control produces measurable error at large distances")
{
    // Demonstrate that naive float (rounding world positions to float32 before
    // computing camera-relative) produces large errors at astronomical distances.
    // This is the "control" that proves the problem exists.

    const double distances[] = {1e3, 1e6, 1e9, 1e12};

    for (double dist : distances)
    {
        // Object 100 m from camera.
        double objX = dist + 100.0;
        double camX = dist;

        // Naive: round to float32 first, then subtract.
        float naiveObj = static_cast<float>(objX);
        float naiveCam = static_cast<float>(camX);
        float naiveRel = naiveObj - naiveCam;

        // Correct: subtract in double, then cast.
        double correctRel = objX - camX;
        float correctRelF = static_cast<float>(correctRel);

        float error = std::fabs(naiveRel - correctRelF);

        // At 1e3 m, error should be ~0 (float can represent 100 m precisely).
        // At 1e12 m, naive float rounds both positions to the same float,
        // producing a completely wrong relative distance (~0 instead of 100).
        if (dist >= 1e9)
        {
            CHECK(error > 1.0f); // naive float produces measurable error at 1e9 m
        }
        if (dist >= 1e12)
        {
            // At 1e12 m, float spacing is ~65 km, so 100 m is invisible.
            // naiveRel should be ~0 (completely wrong).
            CHECK(naiveRel == 0.0f);
            CHECK(error > 50.0f); // error is at least 50 m (half the correct answer)
        }
    }
}