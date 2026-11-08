#include "physics_world.hpp"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

namespace starforge::physics
{
namespace
{

// One-time Jolt registration (allocator, factory, collision handlers).
// The global object unregisters and frees the factory at process exit
// so sanitizers report no leak.
struct GlobalJoltRegistration
{
    GlobalJoltRegistration()
    {
        JPH::RegisterDefaultAllocator();
        // Required by RegisterTypes even when unused for pure
        // code-driven simulations (Jolt HelloWorld pattern).
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    ~GlobalJoltRegistration()
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};

const GlobalJoltRegistration gJoltRegistration;

} // namespace

PhysicsWorld::JoltRegistration::JoltRegistration() = default;

PhysicsWorld::PhysicsWorld(const Config& config)
    : config_(config), broadPhaseFilter_(broadPhaseLayers_, objectLayerFilter_)
{
    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(config.tempAllocatorBytes);
    jobSystem_ = std::make_unique<JobsAdapter>(1024);
    system_.Init(config.maxBodies, 0, config.maxBodyPairs, config.maxContactConstraints, broadPhaseLayers_,
                 broadPhaseFilter_, objectLayerFilter_);
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::step(float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }
    // Fixed 60 Hz sub-stepping with remainder carry-over, so results do
    // not depend on the caller's frame pacing.
    constexpr float kFixedStep = 1.0f / 60.0f;
    stepRemainder_ += dt;
    while (stepRemainder_ >= kFixedStep)
    {
        system_.Update(kFixedStep, 1, tempAllocator_.get(), jobSystem_.get());
        stepRemainder_ -= kFixedStep;
    }
}

BodyHandle PhysicsWorld::createStaticBox(float halfX, float halfY, float halfZ, float x, float y, float z)
{
    JPH::BoxShapeSettings shapeSettings(JPH::Vec3(halfX, halfY, halfZ));
    shapeSettings.SetEmbedded();
    JPH::ShapeRefC shape = shapeSettings.Create().Get();
    JPH::BodyCreationSettings settings(shape, JPH::RVec3(x, y, z), JPH::Quat::sIdentity(), JPH::EMotionType::Static,
                                       Layers::NonMoving);
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
    JPH::BodyCreationSettings settings(new JPH::SphereShape(radius), JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Dynamic, Layers::Moving);
    settings.mMassPropertiesOverride.mMass = mass;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    auto& bodyInterface = system_.GetBodyInterface();
    return {bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate)};
}

JPH::Vec3 PhysicsWorld::getPosition(BodyHandle body) const
{
    return system_.GetBodyInterface().GetPosition(body.id);
}

JPH::Vec3 PhysicsWorld::getLinearVelocity(BodyHandle body) const
{
    return system_.GetBodyInterface().GetLinearVelocity(body.id);
}

void PhysicsWorld::setLinearVelocity(BodyHandle body, JPH::Vec3 velocity)
{
    auto& bodyInterface = system_.GetBodyInterface();
    bodyInterface.SetLinearVelocity(body.id, velocity);
    // Velocity changes must wake sleeping bodies.
    bodyInterface.ActivateBody(body.id);
}

void PhysicsWorld::activateBody(BodyHandle body)
{
    system_.GetBodyInterface().ActivateBody(body.id);
}

void PhysicsWorld::destroyBody(BodyHandle body)
{
    auto& bodyInterface = system_.GetBodyInterface();
    bodyInterface.RemoveBody(body.id);
    bodyInterface.DestroyBody(body.id);
}

} // namespace starforge::physics
