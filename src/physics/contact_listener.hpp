#pragma once

// Jolt contact listener (M11.3): callbacks run on Jolt worker threads
// and only push into the deferred ContactQueue. No Flecs access here.

#include "contact_queue.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>

namespace starforge::physics
{

class PhysicsContactListener final : public JPH::ContactListener
{
public:
    explicit PhysicsContactListener(ContactQueue& queue) : queue_(queue)
    {
    }

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override;

    void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override
    {
        (void)body1;
        (void)body2;
        (void)manifold;
        (void)settings;
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& subShapeIDPair) override
    {
        (void)subShapeIDPair;
    }

private:
    ContactQueue& queue_;
};

} // namespace starforge::physics
