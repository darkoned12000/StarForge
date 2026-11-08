#pragma once

// Collision layers and filters (Jolt HelloWorld pattern).
// NonMoving: static world geometry. Moving: ships, asteroids, debris.
// Projectile: fast movers (CCD in later steps). Sensor: triggers.

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>

namespace starforge::physics
{

namespace Layers
{
static constexpr JPH::ObjectLayer NonMoving = 0;
static constexpr JPH::ObjectLayer Moving = 1;
static constexpr JPH::ObjectLayer Projectile = 2;
static constexpr JPH::ObjectLayer Sensor = 3;
static constexpr JPH::ObjectLayer NumLayers = 4;
} // namespace Layers

namespace BroadPhaseLayers
{
static constexpr JPH::BroadPhaseLayer NonMoving{0};
static constexpr JPH::BroadPhaseLayer Moving{1};
static constexpr JPH::BroadPhaseLayer NumLayers{2};
} // namespace BroadPhaseLayers

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterfaceTable
{
public:
    BroadPhaseLayerInterfaceImpl() : JPH::BroadPhaseLayerInterfaceTable(static_cast<std::uint32_t>(Layers::NumLayers), 2u)
    {
        MapObjectToBroadPhaseLayer(Layers::NonMoving, BroadPhaseLayers::NonMoving);
        MapObjectToBroadPhaseLayer(Layers::Moving, BroadPhaseLayers::Moving);
        MapObjectToBroadPhaseLayer(Layers::Projectile, BroadPhaseLayers::Moving);
        MapObjectToBroadPhaseLayer(Layers::Sensor, BroadPhaseLayers::NonMoving);
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilterTable
{
public:
    ObjectLayerPairFilterImpl() : JPH::ObjectLayerPairFilterTable(Layers::NumLayers)
    {
        EnableCollision(Layers::NonMoving, Layers::Moving);
        EnableCollision(Layers::NonMoving, Layers::Projectile);
        EnableCollision(Layers::NonMoving, Layers::Sensor);
        EnableCollision(Layers::Moving, Layers::Moving);
        EnableCollision(Layers::Moving, Layers::Projectile);
        EnableCollision(Layers::Moving, Layers::Sensor);
        EnableCollision(Layers::Projectile, Layers::Sensor);
    }
};

// Derived from the pair filter, so broadphase filtering follows the
// same enable/disable decisions.
class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilterTable
{
public:
    ObjectVsBroadPhaseLayerFilterImpl(
        const JPH::BroadPhaseLayerInterface& broadPhaseInterface, const JPH::ObjectLayerPairFilter& pairFilter)
        : JPH::ObjectVsBroadPhaseLayerFilterTable(
              broadPhaseInterface, 2u, pairFilter, static_cast<std::uint32_t>(Layers::NumLayers))
    {
    }
};

} // namespace starforge::physics
