#include <doctest/doctest.h>

#include "jobs/job_scheduler.hpp"
#include "planets/cubed_sphere.hpp"
#include "planets/planet_mesh.hpp"
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

TEST_CASE("Cubed sphere: zero vector and deprecated wrappers")
{
    CubeFace face = CubeFace::PositiveX;
    double u = 0.0;
    double v = 0.0;
    CHECK_FALSE(sphereToCube(Vec3d{0.0, 0.0, 0.0}, face, u, v));

    // Deprecated float wrappers delegate to double API.
    float pos[3] = {};
    CubedSphere::faceToSphere(static_cast<int32_t>(CubeFace::PositiveY), 0.25f, -0.5f, pos);
    const Vec3d expected = cubeToSphere(CubeFace::PositiveY, 0.25, -0.5);
    CHECK(std::abs(static_cast<double>(pos[0]) - expected.x) < 1e-6);
    CHECK(std::abs(static_cast<double>(pos[1]) - expected.y) < 1e-6);
    CHECK(std::abs(static_cast<double>(pos[2]) - expected.z) < 1e-6);

    CubeFace outFace = CubeFace::NegativeX;
    float outU = 0.0f;
    float outV = 0.0f;
    CHECK(CubedSphere::sphereToFace(pos, &outFace, &outU, &outV));
    CHECK(outFace == CubeFace::PositiveY);

    CHECK_FALSE(CubedSphere::sphereToFace(nullptr, &outFace, &outU, &outV));
    float zero[3] = {0.0f, 0.0f, 0.0f};
    CHECK_FALSE(CubedSphere::sphereToFace(zero, &outFace, &outU, &outV));
}

TEST_CASE("Cube patch UV bounds: shared utility matches quadtree subdivision")
{
    const auto root = cubePatchUvBounds(0, 0, 0);
    CHECK(root.uMin == -1.0);
    CHECK(root.uMax == 1.0);
    CHECK(root.vMin == -1.0);
    CHECK(root.vMax == 1.0);

    const auto child = cubePatchUvBounds(1, 0, 1);
    CHECK(child.uMin == 0.0);
    CHECK(child.uMax == 1.0);
    CHECK(child.vMin == -1.0);
    CHECK(child.vMax == 0.0);

    // Deep level stays within [-1,1] and has expected span.
    const auto deep = cubePatchUvBounds(3, 5, 4);
    const double span = 2.0 / 16.0;
    CHECK(std::abs((deep.uMax - deep.uMin) - span) < 1e-12);
    CHECK(std::abs((deep.vMax - deep.vMin) - span) < 1e-12);
}

TEST_CASE("Planet patch: bounding sphere is conservative for sampled surface points")
{
    const double radius = 6371000.0;
    PlanetPatchGenerator gen(radius, 1.0);
    const auto patch = gen.createRootPatch(CubeFace::PositiveZ);

    for (int iy = 0; iy <= 16; ++iy)
    {
        const double v = patch.vMin + (patch.vMax - patch.vMin) * (static_cast<double>(iy) / 16.0);
        for (int ix = 0; ix <= 16; ++ix)
        {
            const double u = patch.uMin + (patch.uMax - patch.uMin) * (static_cast<double>(ix) / 16.0);
            const Vec3d p = radius * cubeToSphere(patch.face, u, v);
            const Vec3d d = p - patch.center;
            const double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            CHECK(dist <= patch.boundingRadius + 1e-6);
        }
    }
}

TEST_CASE("Planet quadtree: in-face 2:1 balance enforced")
{
    PlanetQuadtree qt(CubeFace::PositiveX);
    const Vec3d planetCenter{0.0, 0.0, 0.0};
    // Place camera close to one corner to force uneven split.
    const Vec3d cameraPos{6371000.0 * 2.0, 6371000.0 * 0.9, 6371000.0 * 0.9};
    const double planetRadius = 6371000.0;

    for (int i = 0; i < 6; ++i)
    {
        qt.update(planetCenter, cameraPos, planetRadius, 0.0005, 0.00025);
    }

    CHECK(qt.checkAdjacencyInvariant());

    // Stronger direct check: no in-face edge-adjacent leaves differ by > 1.
    const auto leaves = qt.getLeaves();
    for (const auto* leaf : leaves)
    {
        const int extent = 1 << leaf->level;
        const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& dir : dirs)
        {
            const int nx = leaf->x + dir[0];
            const int ny = leaf->y + dir[1];
            if (nx < 0 || ny < 0 || nx >= extent || ny >= extent)
            {
                continue; // Cross-face edge: single-face quadtree.
            }

            bool found = false;
            for (const auto* other : leaves)
            {
                if (other == leaf)
                {
                    continue;
                }

                const int shift = leaf->level - other->level;
                if (shift >= 0)
                {
                    // Other is same level or shallower: covers full neighbor cell.
                    const int tx = nx >> shift;
                    const int ty = ny >> shift;
                    if (tx == other->x && ty == other->y)
                    {
                        INFO("leaf=" << leaf->level << "," << leaf->x << "," << leaf->y << " other=" << other->level
                                      << "," << other->x << "," << other->y << " dir=" << dir[0] << "," << dir[1]);
                        CHECK(leaf->level - other->level <= 1);
                        found = true;
                    }
                }
                else
                {
                    // Other is deeper: only count it if it sits on the shared border.
                    const int invShift = -shift;
                    const int ox = other->x >> invShift;
                    const int oy = other->y >> invShift;
                    if (ox != nx || oy != ny)
                    {
                        continue;
                    }

                    const int scale = 1 << invShift;
                    const bool borderAdjacent =
                        (dir[0] == 1 && other->x == nx * scale) ||
                        (dir[0] == -1 && (other->x + 1) == (nx + 1) * scale) ||
                        (dir[1] == 1 && other->y == ny * scale) ||
                        (dir[1] == -1 && (other->y + 1) == (ny + 1) * scale);
                    if (borderAdjacent)
                    {
                        INFO("leaf=" << leaf->level << "," << leaf->x << "," << leaf->y << " other=" << other->level
                                      << "," << other->x << "," << other->y << " dir=" << dir[0] << "," << dir[1]);
                        CHECK(other->level - leaf->level <= 1);
                        found = true;
                    }
                }
            }
            CHECK(found);
        }
    }
}
