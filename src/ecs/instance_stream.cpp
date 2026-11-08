#include "instance_stream.hpp"

#include "spatial/camera_relative.hpp"

namespace starforge::ecs
{
namespace
{

[[nodiscard]] environment::AsteroidInstance toInstance(const Transform& transform, const spatial::WorldPosition& camera)
{
    const auto relative = spatial::toCameraRelative(transform.position, camera);
    environment::AsteroidInstance instance;
    instance.posScale = math::Vec4f{
        static_cast<float>(relative.x),
        static_cast<float>(relative.y),
        static_cast<float>(relative.z),
        transform.scale,
    };
    instance.rotation = transform.orientation;
    return instance;
}

} // namespace

std::uint32_t collectAsteroidInstances(
    const Ecs& world, const spatial::WorldPosition& camera, std::vector<environment::AsteroidInstance>& out)
{
    out.clear();
    // Cached query (no per-frame recompilation). each() walks Flecs
    // archetype chunks internally; the per-entity callback cost is
    // dominated by the double-precision camera-relative conversion.
    world.asteroidQuery().each([&out, &camera](flecs::entity, const Transform& transform, const AsteroidTag&) {
        out.push_back(toInstance(transform, camera));
    });
    return static_cast<std::uint32_t>(out.size());
}

std::uint32_t collectAsteroidInstances(
    const Ecs& world, const SpatialIndex& index, const spatial::WorldPosition& camera,
    std::vector<environment::AsteroidInstance>& out)
{
    (void)world; // Handles resolve components directly; index drives culling.
    out.clear();
    for (const auto entity : index.findNeighborhood(camera))
    {
        if (!entity.has<AsteroidTag>())
        {
            continue;
        }
        const auto& transform = entity.get<Transform>();
        out.push_back(toInstance(transform, camera));
    }
    return static_cast<std::uint32_t>(out.size());
}

} // namespace starforge::ecs
