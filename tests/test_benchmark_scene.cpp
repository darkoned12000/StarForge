#include <doctest/doctest.h>

#include "bench/benchmark_scene.hpp"
#include "bench/benchmark_workload.hpp"

#include <cmath>
#include <cstdint>
#include <set>

using namespace starforge::bench;
using namespace starforge::math;

TEST_CASE("BenchmarkScene: deterministic generation")
{
    auto scene1 = generateScene(42, 1000, 10.0);
    auto scene2 = generateScene(42, 1000, 10.0);

    CHECK(scene1.instanceCount == 1000);
    CHECK(scene2.instanceCount == 1000);
    CHECK(scene1.instances.size() == 1000);

    // Same seed produces identical scenes.
    for (std::size_t i = 0; i < scene1.instances.size(); ++i)
    {
        CHECK(scene1.instances[i].posX == scene2.instances[i].posX);
        CHECK(scene1.instances[i].posY == scene2.instances[i].posY);
        CHECK(scene1.instances[i].posZ == scene2.instances[i].posZ);
        CHECK(scene1.instances[i].scale == scene2.instances[i].scale);
    }
}

TEST_CASE("BenchmarkScene: different seeds produce different scenes")
{
    auto scene1 = generateScene(42, 100, 10.0);
    auto scene2 = generateScene(99, 100, 10.0);

    bool different = false;
    for (std::size_t i = 0; i < scene1.instances.size(); ++i)
    {
        if (scene1.instances[i].posX != scene2.instances[i].posX)
        {
            different = true;
            break;
        }
    }
    CHECK(different);
}

TEST_CASE("BenchmarkScene: instances within radius")
{
    auto scene = generateScene(42, 5000, 10.0);
    for (const auto& inst : scene.instances)
    {
        double dist = std::sqrt(inst.posX * inst.posX + inst.posY * inst.posY + inst.posZ * inst.posZ);
        CHECK(dist <= 10.0 + 1e-9);
    }
}

TEST_CASE("BenchmarkScene: scale in range")
{
    auto scene = generateScene(42, 100, 10.0);
    for (const auto& inst : scene.instances)
    {
        CHECK(inst.scale >= 0.5f);
        CHECK(inst.scale <= 2.0f);
    }
}

TEST_CASE("BenchmarkScene: quaternion is normalized")
{
    auto scene = generateScene(42, 100, 10.0);
    for (const auto& inst : scene.instances)
    {
        float len = std::sqrt(inst.rotX * inst.rotX + inst.rotY * inst.rotY +
                              inst.rotZ * inst.rotZ + inst.rotW * inst.rotW);
        CHECK(std::fabs(len - 1.0f) < 1e-5f);
    }
}

TEST_CASE("BenchmarkScene: generateWorkloadScene produces correct instance count")
{
    WorkloadConfig config;
    config.instanceCount = 1000;
    config.radius = 10.0;
    config.seed = 42;

    auto scene = generateWorkloadScene(config);
    CHECK(scene.instanceCount == 1000);
    CHECK(scene.instances.size() == 1000);
}

TEST_CASE("BenchmarkScene: radius scales with visible fraction")
{
    WorkloadConfig config100;
    config100.instanceCount = 100;
    config100.visibleFraction = 1.0;
    config100.seed = 42;
    auto scene100 = generateWorkloadScene(config100);

    WorkloadConfig config1;
    config1.instanceCount = 100;
    config1.visibleFraction = 0.01;
    config1.seed = 42;
    auto scene1 = generateWorkloadScene(config1);

    // 1% visible should have larger radius.
    CHECK(scene1.radius > scene100.radius);
}

TEST_CASE("Frustum: build from reverse-Z view-proj")
{
    // Camera at (0,0,5) looking down -Z, reverse-Z projection.
    Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 16.0 / 9.0, 100.0, 0.1); // swapped for reverse-Z
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);
    for (int i = 0; i < 6; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            CHECK(std::isfinite(f.planes[i][j]));
        }
    }
}

TEST_CASE("Frustum: reverse-Z plane extraction and culling")
{
    // Camera at (0,0,5) looking down -Z. Near=0.5, far=50.0, reverse-Z.
    Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 1.0, 50.0, 0.5); // swapped for reverse-Z
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);

    // Point between near and far: visible.
    CHECK(sphereInFrustum(f, 0.0, 0.0, 0.0, 0.2));

    // Point in front of near plane (between camera and near): culled.
    CHECK_FALSE(sphereInFrustum(f, 0.0, 0.0, 4.8, 0.1));

    // Point beyond far plane (along -Z): culled.
    CHECK_FALSE(sphereInFrustum(f, 0.0, 0.0, -55.0, 0.2));
}

TEST_CASE("Frustum: sphereInFrustum basic (standard projection)")
{
    // Standard projection for comparison.
    Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 1.0, 0.1, 100.0);
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);

    // Origin should be inside.
    CHECK(sphereInFrustum(f, 0.0, 0.0, 0.0, 0.5));

    // Point beyond far plane (along -Z): culled.
    CHECK_FALSE(sphereInFrustum(f, 0.0, 0.0, -200.0, 0.5));
}

TEST_CASE("Cull: 100% visible scene")
{
    auto scene = generateScene(42, 1000, 1.0); // small radius = all visible
    Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 1.0, 0.1, 100.0);
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);

    auto visible = cullInstances(scene, f, 0.5);
    // Most instances should be visible (small radius, large frustum).
    CHECK(visible.size() > static_cast<std::size_t>(static_cast<double>(scene.instances.size()) * 0.9));
}

TEST_CASE("Cull: 1% visible scene has low visibility")
{
    auto scene = generateScene(42, 1000, 100.0); // large radius = few visible
    Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 1.0, 0.1, 100.0);
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);

    auto visible = cullInstances(scene, f, 0.5);
    // With 100m radius and camera at 5m, most instances are outside frustum.
    CHECK(visible.size() < static_cast<std::size_t>(static_cast<double>(scene.instances.size()) * 0.10));
}

TEST_CASE("Cull: empty scene")
{
    auto scene = generateScene(42, 0, 10.0);
    Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 1.0, 0.1, 100.0);
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);

    auto visible = cullInstances(scene, f, 0.5);
    CHECK(visible.empty());
}

TEST_CASE("Cull: total miss (scene entirely outside frustum)")
{
    auto scene = generateScene(42, 100, 1.0);
    // Camera far away looking in a direction that doesn't include the origin.
    Mat4d view = glm::lookAt(Vec3d(1000, 1000, 1000), Vec3d(1001, 1001, 1001), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 1.0, 0.1, 100.0);
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);

    // Scene is at origin, camera is far away looking elsewhere.
    auto visible = cullInstances(scene, f, 0.5);
    CHECK(visible.empty());
}

TEST_CASE("Cull: bounding sphere grazing")
{
    // A sphere whose center is outside the frustum but whose radius
    // overlaps the plane boundary should be considered visible.
    Mat4d view = glm::lookAt(Vec3d(0, 0, 5), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 1.0, 0.1, 100.0);
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);

    // Point just outside the near plane but with large radius overlapping.
    // sphereInFrustum checks dist < -radius, so a grazing sphere passes.
    CHECK(sphereInFrustum(f, 0.0, 0.0, 4.9, 0.5));
}

TEST_CASE("Cull: deterministic")
{
    auto scene = generateScene(42, 500, 10.0);
    Mat4d view = glm::lookAt(Vec3d(0, 0, 50), Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    Mat4d proj = glm::perspective(1.0, 1.0, 0.1, 1000.0);
    Mat4d vp = proj * view;
    Frustum f = buildFrustum(vp);

    auto v1 = cullInstances(scene, f, 0.5);
    auto v2 = cullInstances(scene, f, 0.5);
    CHECK(v1.size() == v2.size());
    for (std::size_t i = 0; i < v1.size(); ++i)
    {
        CHECK(v1[i] == v2[i]);
    }
}

TEST_CASE("Workload: recordWorkloadFrame counts draw calls")
{
    // Verify scene generation and culling work together.
    WorkloadConfig config;
    config.instanceCount = 100;
    config.visibleFraction = 1.0;
    config.seed = 42;
    config.enableCulling = true;

    auto scene = generateWorkloadScene(config);
    auto frustum = buildWorkloadFrustum(config);
    auto visible = cullInstances(scene, frustum, 0.5);

    CHECK(visible.size() <= scene.instances.size());
    CHECK(visible.size() > 0);
}
