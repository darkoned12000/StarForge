#include "contact_listener.hpp"

#include <Jolt/Physics/Body/Body.h>

namespace starforge::physics
{

void PhysicsContactListener::OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                                            const JPH::ContactManifold& manifold, JPH::ContactSettings& settings)
{
    (void)settings;
    const auto idA = static_cast<std::uint64_t>(body1.GetUserData());
    const auto idB = static_cast<std::uint64_t>(body2.GetUserData());
    if (idA == 0 && idB == 0)
    {
        return; // Both bodies are world geometry (e.g. static floor).
    }
    // One-sided pairs (entity vs. sensor/floor geometry) still emit;
    // flush only targets the valid side.

    // Sensor manifolds may carry zero contact points; fall back to the
    // body center of mass instead of reading out of bounds.
    const JPH::RVec3 point =
        manifold.mRelativeContactPointsOn1.empty() ? body1.GetCenterOfMassPosition()
                                                   : manifold.GetWorldSpaceContactPointOn1(0);
    const JPH::Vec3 relativeVelocity = body1.GetLinearVelocity() - body2.GetLinearVelocity();

    queue_.push({
        .idA = idA,
        .idB = idB,
        .islandPoint = {point.GetX(), point.GetY(), point.GetZ()},
        .normal = {manifold.mWorldSpaceNormal.GetX(), manifold.mWorldSpaceNormal.GetY(), manifold.mWorldSpaceNormal.GetZ()},
        .impulse = relativeVelocity.Length(),
    });
}

} // namespace starforge::physics
