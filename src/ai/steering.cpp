#include "ai/steering.hpp"

#include "spatial/camera_relative.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace starforge::ai
{

SteeringParams steeringParamsFor(const AiAgent& agent, double arrivalRadius) noexcept
{
    SteeringParams params;
    params.maxSpeed = agent.maxSpeed;
    params.maxAcceleration = agent.maxAcceleration;
    params.arrivalRadius = arrivalRadius;
    params.separationRadius = 25.0;
    params.separationWeight = 1.0;
    params.turnRate = agent.turnRate;
    return params;
}

std::size_t collectNeighbours(flecs::entity self, const std::vector<flecs::entity>& candidates,
                              const ecs::Transform& selfTransform, std::array<math::Vec3d, 32>& out)
{
    std::size_t count = 0;
    for (const auto other : candidates)
    {
        if (other == self || !other.is_alive() || !other.has<ecs::Transform>() ||
            other.has<ecs::StationTag>())
        {
            continue;
        }
        const math::Vec3d relative =
            spatial::toCameraRelative(other.get<ecs::Transform>().position, selfTransform.position);
        if (glm::length(relative) <= 100.0 && count < out.size())
        {
            out[count++] = relative;
        }
    }
    return count;
}

double arrivalSpeed(double distance, const SteeringParams& params) noexcept
{
    if (distance <= params.arrivalRadius || params.maxAcceleration <= 0.0 || params.maxSpeed <= 0.0)
    {
        return 0.0;
    }

    const double brakingDistance = std::max(0.0, distance - params.arrivalRadius);
    const double brakingSpeed = std::sqrt(2.0 * params.maxAcceleration * brakingDistance);
    return std::min(params.maxSpeed, brakingSpeed);
}

math::Vec3d seekVelocity(const math::Vec3d& currentPos, const math::Vec3d& targetPos,
                         const math::Vec3d& currentVelocity, const SteeringParams& params,
                         double dt) noexcept
{
    const math::Vec3d toTarget = targetPos - currentPos;
    const double distance = glm::length(toTarget);
    math::Vec3d desired{0.0, 0.0, 0.0};
    if (distance > params.arrivalRadius && dt > 0.0)
    {
        const math::Vec3d direction = toTarget / distance;
        const double speed = arrivalSpeed(distance, params);
        desired = direction * speed;
    }

    const double accelerationLimit = params.maxAcceleration * dt;
    if (accelerationLimit > 0.0)
    {
        const math::Vec3d delta = desired - currentVelocity;
        const double deltaLength = glm::length(delta);
        if (deltaLength > accelerationLimit)
        {
            desired = currentVelocity + delta * (accelerationLimit / deltaLength);
        }
    }
    return desired;
}

math::Vec3d separationVelocity(const math::Vec3d& currentPos,
                               std::span<const math::Vec3d> otherPositions,
                               const SteeringParams& params) noexcept
{
    math::Vec3d result{0.0, 0.0, 0.0};
    if (params.separationRadius <= 0.0)
    {
        return result;
    }

    for (const auto& other : otherPositions)
    {
        const math::Vec3d away = currentPos - other;
        const double distance = glm::length(away);
        if (distance < 1.0e-6)
        {
            // Exact overlap: break the symmetry with a deterministic push.
            result.x += params.maxSpeed * params.separationWeight;
            continue;
        }
        if (distance >= params.separationRadius)
        {
            continue;
        }

        const double falloff = 1.0 - (distance / params.separationRadius);
        result += (away / distance) * (falloff * falloff * params.maxSpeed * params.separationWeight);
    }
    return result;
}

math::Vec3d separationVelocity(const math::Vec3d& currentPos,
                               const std::vector<math::Vec3d>& otherPositions,
                               const SteeringParams& params) noexcept
{
    return separationVelocity(currentPos, std::span<const math::Vec3d>(otherPositions), params);
}

math::Vec4f headingForDirection(const math::Vec3d& dir) noexcept
{
    if (glm::length(dir) <= 1.0e-12)
    {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    const glm::vec3 direction = glm::normalize(glm::vec3{static_cast<float>(dir.x),
                                                          static_cast<float>(dir.y),
                                                          static_cast<float>(dir.z)});
    const glm::vec3 forward{0.0f, 0.0f, -1.0f};

    glm::quat rotation;
    const float alignment = glm::dot(forward, direction);
    if (alignment > 0.999999f)
    {
        rotation = glm::identity<glm::quat>();
    }
    else if (alignment < -0.999999f)
    {
        rotation = glm::angleAxis(glm::pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
    }
    else
    {
        const glm::vec3 axis = glm::normalize(glm::cross(forward, direction));
        const float angle = std::acos(glm::clamp(glm::dot(forward, direction), -1.0f, 1.0f));
        rotation = glm::angleAxis(angle, axis);
    }

    return math::Vec4f{rotation.x, rotation.y, rotation.z, rotation.w};
}

void steerEntity(flecs::entity entity, const spatial::WorldPosition& targetPos, double dt,
                 const SteeringParams& params, std::span<const math::Vec3d> otherPositions)
{
    const auto& transform = entity.get<ecs::Transform>();
    // Copy the orientation now: adding Velocity below can move the entity
    // to a new archetype and invalidate `transform` before we read it.
    const math::Vec4f startOrientation = transform.orientation;
    const math::Vec3d targetRelative = spatial::toCameraRelative(targetPos, transform.position);

    math::Vec3d currentVelocity{0.0, 0.0, 0.0};
    if (entity.has<ecs::Velocity>())
    {
        currentVelocity = entity.get<ecs::Velocity>().linear;
    }

    math::Vec3d desired = seekVelocity({0.0, 0.0, 0.0}, targetRelative, currentVelocity, params, dt);
    desired += separationVelocity({0.0, 0.0, 0.0}, otherPositions, params);

    // Re-clamp the composite command so separation cannot bypass the speed
    // and acceleration budgets.
    if (params.maxSpeed > 0.0 && glm::length(desired) > params.maxSpeed)
    {
        desired = glm::normalize(desired) * params.maxSpeed;
    }
    const double accelerationStep = params.maxAcceleration * dt;
    if (accelerationStep > 0.0)
    {
        const math::Vec3d delta = desired - currentVelocity;
        const double deltaLength = glm::length(delta);
        if (deltaLength > accelerationStep)
        {
            desired = currentVelocity + delta * (accelerationStep / deltaLength);
        }
    }

    entity.set<ecs::Velocity>({desired, {0.0f, 0.0f, 0.0f}});

    auto& mutableTransform = entity.get_mut<ecs::Transform>();
    if (glm::length(targetRelative) > params.arrivalRadius)
    {
        const auto targetHeading = headingForDirection(targetRelative);
        if (params.turnRate > 0.0 && dt > 0.0)
        {
            // Rate-limit the yaw so ships rotate with momentum instead of
            // snapping. Slerp from the current orientation toward the
            // target heading, clamped to turnRate * dt radians.
            const glm::quat currentQuat{startOrientation.w, startOrientation.x,
                                        startOrientation.y, startOrientation.z};
            const glm::quat targetQuat{targetHeading.w, targetHeading.x, targetHeading.y, targetHeading.z};
            float cosHalf = std::clamp(std::abs(glm::dot(currentQuat, targetQuat)), 0.0f, 1.0f);
            const float halfAngle = std::acos(cosHalf);
            const double maxHalfAngle = params.turnRate * dt * 0.5;
            if (halfAngle > maxHalfAngle)
            {
                const glm::quat stepped = glm::slerp(currentQuat, targetQuat,
                                                     static_cast<float>(maxHalfAngle / halfAngle));
                mutableTransform.orientation = {stepped.x, stepped.y, stepped.z, stepped.w};
            }
            else
            {
                mutableTransform.orientation = targetHeading;
            }
        }
        else
        {
            mutableTransform.orientation = targetHeading;
        }
    }
    entity.modified<ecs::Transform>();
}

void steerEntity(flecs::entity entity, const spatial::WorldPosition& targetPos, double dt,
                 const SteeringParams& params, const std::vector<math::Vec3d>& otherPositions)
{
    steerEntity(entity, targetPos, dt, params, std::span<const math::Vec3d>(otherPositions));
}

} // namespace starforge::ai
