#include "physics_world.hpp"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>

namespace starforge::physics
{

PhysicsWorld::PhysicsWorld(const Config& config)
    : config_(config), tempAllocator_(std::make_unique<JPH::TempAllocatorImpl>(config.tempAllocatorBytes)),
      jobSystem_(std::make_unique<JobsAdapter>(1024)),
      broadPhaseFilter_(broadPhaseLayers_, objectLayerFilter_), contactListener_(contactQueue_)
{
    system_.Init(config.maxBodies, 0, config.maxBodyPairs, config.maxContactConstraints, broadPhaseLayers_,
                 broadPhaseFilter_, objectLayerFilter_);
    system_.SetContactListener(&contactListener_);
}

void PhysicsWorld::flushContacts(ecs::Ecs& world, const spatial::WorldPosition& camera)
{
    contactQueue_.flush(world, camera);
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::step(float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }
    // Fixed 60 Hz sub-stepping with remainder carry-over, so results do
    // not depend on the caller's frame pacing. The accumulator is clamped
    // to 4 sub-steps so a frame hitch cannot spiral into lockup.
    constexpr float kFixedStep = 1.0f / 60.0f;
    constexpr float kMaxAccumulated = kFixedStep * 4.0f;
    stepRemainder_ = std::min(stepRemainder_ + dt, kMaxAccumulated);
    while (stepRemainder_ >= kFixedStep)
    {
        system_.Update(kFixedStep, 1, tempAllocator_.get(), jobSystem_.get());
        stepRemainder_ -= kFixedStep;
    }
}

BodyHandle PhysicsWorld::createStaticBox(float halfX, float halfY, float halfZ, float x, float y, float z)
{
    if (!(halfX > 0.0f) || !(halfY > 0.0f) || !(halfZ > 0.0f))
    {
        return {};
    }
    JPH::BoxShapeSettings shapeSettings(JPH::Vec3(halfX, halfY, halfZ));
    shapeSettings.SetEmbedded();
    const auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError())
    {
        return {};
    }
    JPH::BodyCreationSettings settings(shapeResult.Get(), JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Static, Layers::NonMoving);
    auto& bodyInterface = system_.GetBodyInterface();
    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (body == nullptr)
    {
        return {};
    }
    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return {body->GetID()};
}

BodyHandle PhysicsWorld::createDynamicSphere(float radius, float mass, float x, float y, float z)
{
    if (!(radius > 0.0f) || !(mass > 0.0f))
    {
        return {};
    }
    JPH::BodyCreationSettings settings(new JPH::SphereShape(radius), JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Dynamic, Layers::Moving);
    settings.mMassPropertiesOverride.mMass = mass;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    auto& bodyInterface = system_.GetBodyInterface();
    return {bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate)};
}

void PhysicsWorld::setMotionQualityLinearCast(BodyHandle body)
{
    if (!body.isValid())
    {
        return;
    }
    system_.GetBodyInterface().SetMotionQuality(body.id, JPH::EMotionQuality::LinearCast);
}

BodyHandle PhysicsWorld::createSensorBox(float halfX, float halfY, float halfZ, float x, float y, float z)
{
    if (!(halfX > 0.0f) || !(halfY > 0.0f) || !(halfZ > 0.0f))
    {
        return {};
    }
    JPH::BoxShapeSettings shapeSettings(JPH::Vec3(halfX, halfY, halfZ));
    shapeSettings.SetEmbedded();
    const auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError())
    {
        return {};
    }
    JPH::BodyCreationSettings settings(shapeResult.Get(), JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Static, Layers::Sensor);
    settings.mIsSensor = true;
    auto& bodyInterface = system_.GetBodyInterface();
    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (body == nullptr)
    {
        return {};
    }
    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return {body->GetID()};
}

namespace
{

// Raycast object-layer filter with optional sensor inclusion.
class RaycastObjectLayerFilter final : public JPH::ObjectLayerFilter
{
public:
    RaycastObjectLayerFilter(const JPH::ObjectLayerPairFilter& pairFilter, bool includeSensors)
        : pairFilter_(pairFilter), includeSensors_(includeSensors)
    {
    }

    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return pairFilter_.ShouldCollide(Layers::Moving, layer) && (includeSensors_ || layer != Layers::Sensor);
    }

private:
    const JPH::ObjectLayerPairFilter& pairFilter_;
    bool includeSensors_;
};

// Body filter that skips one body (invalid BodyID skips nothing).
class IgnoreBodyFilter final : public JPH::BodyFilter
{
public:
    explicit IgnoreBodyFilter(JPH::BodyID ignoreBody) : ignoreBody_(ignoreBody) {}

    bool ShouldCollide(const JPH::BodyID& body) const override
    {
        return body != ignoreBody_;
    }

private:
    JPH::BodyID ignoreBody_;
};

} // namespace

std::optional<PhysicsWorld::RayHit>
PhysicsWorld::castRay(const JPH::Vec3& from, const JPH::Vec3& direction, RaycastTarget target) const
{
    return castRay(from, direction, target, BodyHandle{});
}

std::optional<PhysicsWorld::RayHit>
PhysicsWorld::castRay(const JPH::Vec3& from, const JPH::Vec3& direction, RaycastTarget target,
                      BodyHandle ignoreBody) const
{
    if (direction.IsNearZero())
    {
        return std::nullopt;
    }
    JPH::RRayCast ray(JPH::RVec3(from.GetX(), from.GetY(), from.GetZ()),
                      JPH::RVec3(direction.GetX(), direction.GetY(), direction.GetZ()));
    JPH::RayCastResult hit;
    const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(broadPhaseFilter_, Layers::Moving);
    const RaycastObjectLayerFilter objectLayerFilter(objectLayerFilter_, target == RaycastTarget::IncludeSensors);
    // Ignore the shooter's body explicitly: ray origins sit at hull
    // centres, which are inside the shooter's own collider for any ship
    // larger than a point.
    const IgnoreBodyFilter bodyFilter(ignoreBody.id);
    const bool found = system_.GetNarrowPhaseQuery().CastRay(ray, hit, broadPhaseFilter, objectLayerFilter, bodyFilter);
    if (!found)
    {
        return std::nullopt;
    }
    RayHit result;
    result.body = {hit.mBodyID};
    result.fraction = hit.mFraction;
    const JPH::Vec3 point = from + direction * hit.mFraction;
    result.point = point;
    return result;
}

JPH::Vec3 PhysicsWorld::getPosition(BodyHandle body) const
{
    if (!body.isValid())
    {
        return JPH::Vec3::sZero();
    }
    return system_.GetBodyInterface().GetPosition(body.id);
}

JPH::Vec3 PhysicsWorld::getLinearVelocity(BodyHandle body) const
{
    if (!body.isValid())
    {
        return JPH::Vec3::sZero();
    }
    return system_.GetBodyInterface().GetLinearVelocity(body.id);
}

void PhysicsWorld::setLinearVelocity(BodyHandle body, JPH::Vec3 velocity)
{
    if (!body.isValid())
    {
        return;
    }
    auto& bodyInterface = system_.GetBodyInterface();
    bodyInterface.SetLinearVelocity(body.id, velocity);
    // Velocity changes must wake sleeping bodies.
    bodyInterface.ActivateBody(body.id);
}

void PhysicsWorld::activateBody(BodyHandle body)
{
    if (!body.isValid())
    {
        return;
    }
    system_.GetBodyInterface().ActivateBody(body.id);
}

void PhysicsWorld::destroyBody(BodyHandle body)
{
    if (!body.isValid())
    {
        return;
    }
    auto& bodyInterface = system_.GetBodyInterface();
    bodyInterface.RemoveBody(body.id);
    bodyInterface.DestroyBody(body.id);
}

} // namespace starforge::physics
