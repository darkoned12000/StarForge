#include "bridge.hpp"

#include "ecs/components.hpp"
#include "spatial/camera_relative.hpp"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

namespace starforge::physics
{
namespace
{

[[nodiscard]] JPH::Vec3 toJolt(const spatial::WorldPosition& position, const spatial::WorldPosition& camera)
{
    const auto relative = spatial::toCameraRelative(position, camera);
    return JPH::Vec3(static_cast<float>(relative.x), static_cast<float>(relative.y), static_cast<float>(relative.z));
}

[[nodiscard]] JPH::Quat toJolt(const math::Vec4f& orientation)
{
    // JPH::Quat is (x, y, z, w), matching our xyzw storage.
    return JPH::Quat(orientation.x, orientation.y, orientation.z, orientation.w);
}

[[nodiscard]] spatial::WorldPosition fromJolt(const JPH::Vec3& relative, const spatial::WorldPosition& camera)
{
    const math::Vec3d delta{static_cast<double>(relative.GetX()), static_cast<double>(relative.GetY()),
                              static_cast<double>(relative.GetZ())};
    auto moved = camera.translated(delta);
    // Camera-relative positions inside the island always fit.
    return moved.has_value() ? *moved : camera;
}

} // namespace

BodyHandle attachBody(ecs::Ecs& world, flecs::entity entity, PhysicsWorld& physics, const spatial::WorldPosition& camera)
{
    const auto& transform = entity.get<ecs::Transform>();
    const auto collider = entity.get<Collider>();
    const JPH::Vec3 position = toJolt(transform.position, camera);

    BodyHandle body;
    if (collider.shape == Collider::Shape::Sphere)
    {
        body = physics.createDynamicSphere(collider.radius, 1.0f, position.GetX(), position.GetY(), position.GetZ());
        if (body.isValid())
        {
            physics.system().GetBodyInterface().SetPositionAndRotation(
                body.id, JPH::RVec3(position.GetX(), position.GetY(), position.GetZ()),
                toJolt(transform.orientation), JPH::EActivation::Activate);
        }
    }
    else
    {
        auto& bodyInterface = physics.system().GetBodyInterface();
        JPH::BoxShapeSettings shapeSettings(
            JPH::Vec3(collider.halfExtents.x, collider.halfExtents.y, collider.halfExtents.z));
        shapeSettings.SetEmbedded();
        JPH::ShapeRefC shape = shapeSettings.Create().Get();
        JPH::BodyCreationSettings settings(shape, JPH::RVec3(position.GetX(), position.GetY(), position.GetZ()),
                                           toJolt(transform.orientation), JPH::EMotionType::Dynamic, Layers::Moving);
        settings.mMassPropertiesOverride.mMass = 1.0f;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        body = {bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate)};
    }

    if (body.isValid())
    {
        // Map the Flecs entity into the body user data so contact
        // callbacks can identify involved entities.
        physics.system().GetBodyInterface().SetUserData(body.id, entity.id());
        entity.set<PhysicsBody>({body});
    }
    return body;
}

void syncEcsToPhysics(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& camera)
{
    auto& bodyInterface = physics.system().GetBodyInterface();
    world.world().query<const ecs::Transform, const PhysicsBody>().each(
        [&bodyInterface, &camera](flecs::entity entity, const ecs::Transform& transform, const PhysicsBody& body) {
            const JPH::Vec3 position = toJolt(transform.position, camera);
            bodyInterface.SetPositionAndRotation(
                body.body.id, JPH::RVec3(position.GetX(), position.GetY(), position.GetZ()),
                toJolt(transform.orientation), JPH::EActivation::Activate);
            if (entity.has<ecs::Velocity>())
            {
                const auto velocity = entity.get<ecs::Velocity>();
                bodyInterface.SetLinearVelocity(
                    body.body.id, JPH::Vec3(static_cast<float>(velocity.linear.x), static_cast<float>(velocity.linear.y),
                                            static_cast<float>(velocity.linear.z)));
            }
        });
}

void detachBody(ecs::Ecs& world, flecs::entity entity, PhysicsWorld& physics)
{
    if (entity.has<PhysicsBody>())
    {
        physics.destroyBody(entity.get<PhysicsBody>().body);
        entity.remove<PhysicsBody>();
    }
    (void)world;
}

void registerBodyCleanup(ecs::Ecs& world, PhysicsWorld& physics)
{
    world.world()
        .observer<const PhysicsBody>()
        .event(flecs::OnRemove)
        .each([&physics](flecs::entity, const PhysicsBody& body) { physics.destroyBody(body.body); });
}

void syncPhysicsToEcs(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& camera)
{
    auto& bodyInterface = physics.system().GetBodyInterface();
    world.world().query<ecs::Transform&, const PhysicsBody>().each(
        [&bodyInterface, &camera](flecs::entity entity, ecs::Transform& transform, const PhysicsBody& body) {
            JPH::RVec3 position;
            JPH::Quat rotation;
            bodyInterface.GetPositionAndRotation(body.body.id, position, rotation);
            transform.position = fromJolt(JPH::Vec3(position.GetX(), position.GetY(), position.GetZ()), camera);
            transform.orientation = math::Vec4f{rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()};
            if (entity.has<ecs::Velocity>())
            {
                auto& velocity = entity.get_mut<ecs::Velocity>();
                const JPH::Vec3 linear = bodyInterface.GetLinearVelocity(body.body.id);
                velocity.linear = math::Vec3d{static_cast<double>(linear.GetX()), static_cast<double>(linear.GetY()),
                                              static_cast<double>(linear.GetZ())};
                entity.modified<ecs::Velocity>();
            }
        });
}

} // namespace starforge::physics
