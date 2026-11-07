#include <doctest/doctest.h>

#include "jobs/job_scheduler.hpp"
#include "planets/cubed_sphere.hpp"
#include "planets/planet_mesh.hpp"
#include "planets/planet_patch.hpp"
#include "planets/planet_quadtree.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace starforge::jobs;
using namespace starforge::planets;
using namespace starforge::math;

TEST_CASE("Cubed sphere: mapping round trip")
{
    for (int face = 0; face < 6; ++face)
    {
        CubeFace f = static_cast<CubeFace>(face);
        for (double u = -0.8; u <= 0.8; u += 0.4)
        {
            for (double v = -0.8; v <= 0.8; v += 0.4)
            {
                Vec3d p = cubeToSphere(f, u, v);
                // Should be on unit sphere
                double len = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
                CHECK(std::abs(len - 1.0) < 1e-10);

                CubeFace f2;
                double u2, v2;
                CHECK(sphereToCube(p, f2, u2, v2));
                CHECK(f2 == f);
                CHECK(std::abs(u2 - u) < 1e-10);
                CHECK(std::abs(v2 - v) < 1e-10);
            }
        }
    }
}

TEST_CASE("Cubed sphere: boundary round trip (world coincidence)")
{
    // Edges and corners are where face assignment is ambiguous; require
    // the round-tripped point to coincide in world space within 1e-9
    // even when f2 differs from f on a shared boundary.
    const double boundary[] = {-1.0, -0.5, 0.0, 0.5, 1.0};
    for (int face = 0; face < 6; ++face)
    {
        const CubeFace f = static_cast<CubeFace>(face);
        for (double u : boundary)
        {
            for (double v : boundary)
            {
                const Vec3d p = cubeToSphere(f, u, v);
                CubeFace f2;
                double u2 = 0.0, v2 = 0.0;
                CHECK(sphereToCube(p, f2, u2, v2));
                const Vec3d p2 = cubeToSphere(f2, u2, v2);
                const double dx = p.x - p2.x;
                const double dy = p.y - p2.y;
                const double dz = p.z - p2.z;
                CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) < 1e-9);
            }
        }
    }
}

TEST_CASE("Cubed sphere: reduced distortion (Jacobian area ratio)")
{
    // True distortion check: the area scaling (Jacobian determinant of
    // the (u,v) -> unit-sphere map) must stay within the gnomonic cube
    // map bound 3*sqrt(3) ~= 5.196 across the whole face.
    const auto jacobianDet = [](CubeFace face, double u, double v) {
        const double h = 1e-4;
        const Vec3d pu1 = cubeToSphere(face, u + h, v);
        const Vec3d pu0 = cubeToSphere(face, u - h, v);
        const Vec3d pv1 = cubeToSphere(face, u, v + h);
        const Vec3d pv0 = cubeToSphere(face, u, v - h);
        const Vec3d du{pu1.x - pu0.x, pu1.y - pu0.y, pu1.z - pu0.z};
        const Vec3d dv{pv1.x - pv0.x, pv1.y - pv0.y, pv1.z - pv0.z};
        // det = |du x dv| / (2h)^2 (central differences).
        const Vec3d cross{
            du.y * dv.z - du.z * dv.y,
            du.z * dv.x - du.x * dv.z,
            du.x * dv.y - du.y * dv.x,
        };
        return std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z) / (4.0 * h * h);
    };

    for (int face = 0; face < 6; ++face)
    {
        const CubeFace f = static_cast<CubeFace>(face);
        double minDet = 1e30;
        double maxDet = 0.0;
        for (double u = -0.9; u <= 0.9; u += 0.3)
        {
            for (double v = -0.9; v <= 0.9; v += 0.3)
            {
                const double det = jacobianDet(f, u, v);
                minDet = std::min(minDet, det);
                maxDet = std::max(maxDet, det);
            }
        }
        CHECK(minDet > 0.0);
        CHECK(maxDet / minDet < 5.196 + 0.1);
    }
}

TEST_CASE("Planet quadtree: hierarchy construction")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    auto leaves = qt.getLeaves();
    CHECK(leaves.size() == 1); // Just root initially
    CHECK(leaves[0]->level == 0);
    CHECK(leaves[0]->face == CubeFace::PositiveX);
}

TEST_CASE("Planet quadtree: adjacency invariant")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    CHECK(qt.checkAdjacencyInvariant());
}

TEST_CASE("Planet quadtree: deep subdivision keeps neighbor LOD balanced")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    Vec3d planetCenter{0.0, 0.0, 0.0};
    // Camera in the visible hemisphere of the +X face (5R, 0, 2R).
    Vec3d cameraPos{6371000.0 * 5.0, 0.0, 6371000.0 * 2.0};
    double planetRadius = 6371000.0;

    // Repeated updates so balancing can propagate (maxLevelDelta per call).
    for (int i = 0; i < 4; ++i)
    {
        auto leaves = qt.update(planetCenter, cameraPos, planetRadius, 0.001, 0.0005);
        CHECK(leaves.size() > 1);
    }

    auto leaves = qt.getLeaves();
    CHECK(leaves.size() > 4); // Deep subdivision happened
    int minLevel = 100;
    int maxLevel = 0;
    for (const auto* leaf : leaves)
    {
        minLevel = std::min(minLevel, leaf->level);
        maxLevel = std::max(maxLevel, leaf->level);
    }
    // Deepest leaves should be levels below the root, and the spread
    // must stay bounded (gradual LOD, not a cliff).
    CHECK(maxLevel >= 3);
    CHECK(maxLevel - minLevel <= 3);
    CHECK(qt.checkAdjacencyInvariant());
}

TEST_CASE("Planet quadtree: split and merge with hysteresis")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    Vec3d planetCenter{0.0, 0.0, 0.0};
    Vec3d cameraPos{10000000.0, 0.0, 0.0}; // Close to planet
    double planetRadius = 6371000.0;
    
    // Update with low threshold to force split
    auto leaves = qt.update(planetCenter, cameraPos, planetRadius, 0.001, 0.0005);
    CHECK(leaves.size() >= 1);
    CHECK(qt.checkAdjacencyInvariant());
    
    // Move camera far away
    cameraPos = Vec3d{100000000.0, 0.0, 0.0};
    leaves = qt.update(planetCenter, cameraPos, planetRadius, 0.001, 0.0005);
    CHECK(qt.checkAdjacencyInvariant());
}

TEST_CASE("Planet quadtree: parallel update matches single-threaded (7.1b)")
{
    const Vec3d planetCenter{0.0, 0.0, 0.0};
    const Vec3d cameraPos{0.0, 0.0, 6371000.0 * 10.0};
    const double planetRadius = 6371000.0;

    PlanetQuadtree sequential(CubeFace::PositiveX);
    auto seqLeaves = sequential.update(planetCenter, cameraPos, planetRadius, 0.001, 0.0005);

    PlanetQuadtree parallel(CubeFace::PositiveX);
    JobScheduler scheduler(SchedulerConfig{.threadCount = 4});
    auto parLeaves = parallel.updateParallel(planetCenter, cameraPos, planetRadius, 0.001, 0.0005, scheduler);

    REQUIRE(seqLeaves.size() == parLeaves.size());
    REQUIRE(seqLeaves.size() > 1); // Non-trivial subdivision happened
    for (std::size_t i = 0; i < seqLeaves.size(); ++i)
    {
        CHECK(parLeaves[i]->level == seqLeaves[i]->level);
        CHECK(parLeaves[i]->x == seqLeaves[i]->x);
        CHECK(parLeaves[i]->y == seqLeaves[i]->y);
        CHECK(parLeaves[i]->face == seqLeaves[i]->face);
    }
    CHECK(parallel.checkAdjacencyInvariant());
}

TEST_CASE("Planet quadtree: bounded patch count")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    Vec3d planetCenter{0.0, 0.0, 0.0};
    Vec3d cameraPos{7000000.0, 0.0, 0.0};
    double planetRadius = 6371000.0;
    
    // Multiple updates along path
    for (int i = 0; i < 10; ++i)
    {
        cameraPos.x -= 50000.0;
        auto leaves = qt.update(planetCenter, cameraPos, planetRadius, 0.01, 0.005);
        CHECK(leaves.size() > 0);
        CHECK(leaves.size() < 1000000); // Bounded
        CHECK(qt.checkAdjacencyInvariant());
    }
}

TEST_CASE("Planet patch mesh: structure and bounds")
{
    const double radius = 6371000.0;
    PlanetPatchGenerator gen(radius, 1.0);
    auto root = gen.createRootPatch(CubeFace::PositiveX);
    auto mesh = generatePatchMesh(root, 4, radius);

    // (n+1)^2 vertices, 6n^2 indices.
    CHECK(mesh.positions.size() == 25);
    CHECK(mesh.normals.size() == 25);
    CHECK(mesh.uvs.size() == 25);
    CHECK(mesh.indices.size() == 96);

    // All indices reference valid vertices.
    for (std::uint32_t idx : mesh.indices)
    {
        CHECK(idx < mesh.positions.size());
    }

    // RTC positions: world position (center + offset) lies on the sphere;
    // offsets stay within the patch bounding radius; normals unit-length.
    for (std::size_t i = 0; i < mesh.positions.size(); ++i)
    {
        const Vec3d world{
            root.center.x + static_cast<double>(mesh.positions[i].x),
            root.center.y + static_cast<double>(mesh.positions[i].y),
            root.center.z + static_cast<double>(mesh.positions[i].z),
        };
        const double len = std::sqrt(world.x * world.x + world.y * world.y + world.z * world.z);
        CHECK(std::abs(len - radius) < 1.0);

        const auto& off = mesh.positions[i];
        const double offLen = std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z);
        CHECK(offLen <= root.boundingRadius + 1.0);

        const auto& n = mesh.normals[i];
        const double nlen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        CHECK(std::abs(nlen - 1.0) < 1e-5);
    }

    // Winding: first triangle normal points outward (dot with world
    // position > 0).
    const auto toWorld = [&root](const Vec3f& off) {
        return Vec3d{
            root.center.x + static_cast<double>(off.x),
            root.center.y + static_cast<double>(off.y),
            root.center.z + static_cast<double>(off.z),
        };
    };
    const Vec3d a = toWorld(mesh.positions[mesh.indices[0]]);
    const Vec3d b = toWorld(mesh.positions[mesh.indices[1]]);
    const Vec3d c = toWorld(mesh.positions[mesh.indices[2]]);
    const Vec3d ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3d ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3d triN{ab.y * ac.z - ab.z * ac.y,
                     ab.z * ac.x - ab.x * ac.z,
                     ab.x * ac.y - ab.y * ac.x};
    CHECK((triN.x * a.x + triN.y * a.y + triN.z * a.z) > 0.0);

    // Degenerate input yields an empty mesh.
    auto empty = generatePatchMesh(root, 0, radius);
    CHECK(empty.positions.empty());
    CHECK(empty.indices.empty());
}

TEST_CASE("Planet patch mesh: adjacent faces share coincident edge vertices (7.2c)")
{
    const double radius = 6371000.0;
    PlanetPatchGenerator gen(radius, 1.0);

    // Full-face meshes on two adjacent faces. The +X face's u=+1 edge
    // must coincide with the -Z face's u=-1 edge in world space.
    auto faceX = gen.createRootPatch(CubeFace::PositiveX);
    auto faceNZ = gen.createRootPatch(CubeFace::NegativeZ);

    auto meshX = generatePatchMesh(faceX, 4, radius);
    auto meshNZ = generatePatchMesh(faceNZ, 4, radius);

    const auto toWorld = [](const PlanetPatch& patch, const Vec3f& off) {
        return Vec3d{
            patch.center.x + static_cast<double>(off.x),
            patch.center.y + static_cast<double>(off.y),
            patch.center.z + static_cast<double>(off.z),
        };
    };

    // Collect the shared edge: +X right column (i == n) vs -Z left column (i == 0).
    auto edgeOf = [&](const PlanetPatch& patch, const PlanetPatchMesh& mesh, bool rightColumn) {
        std::vector<Vec3d> pts;
        const std::uint32_t n = 4;
        const std::uint32_t side = n + 1;
        for (std::uint32_t j = 0; j < side; ++j)
        {
            const std::uint32_t i = rightColumn ? n : 0;
            pts.push_back(toWorld(patch, mesh.positions[j * side + i]));
        }
        return pts;
    };

    auto edgeX = edgeOf(faceX, meshX, true);
    auto edgeNZ = edgeOf(faceNZ, meshNZ, false);
    CHECK(edgeX.size() == edgeNZ.size());
    for (std::size_t i = 0; i < edgeX.size(); ++i)
    {
        const double dx = edgeX[i].x - edgeNZ[i].x;
        const double dy = edgeX[i].y - edgeNZ[i].y;
        const double dz = edgeX[i].z - edgeNZ[i].z;
        CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) < 1.0);
    }
}
