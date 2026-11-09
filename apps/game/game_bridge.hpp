#pragma once

// Bridge between the game loop (public API) and the ECS world.
// Plain data only: no TU that calls the bridge ever includes both the
// public math header and the internal GLM-based math aliases (the same
// rule the public API bridges use).

#include <memory>

namespace starforge::game
{

struct RawVec3d
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct RawVec3f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RawVec4f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct RawTransform
{
    RawVec3d position{};
    RawVec4f orientation{}; // xyzw, unit
    float scale = 1.0f;
};

// Owning wrapper around the ECS world with one player ship.
// The ship always exists; this keeps the game loop simple.
class GameWorld
{
public:
    GameWorld();
    ~GameWorld();

    GameWorld(GameWorld&& other) noexcept;
    GameWorld& operator=(GameWorld&& other) noexcept;
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;

    // Set the ship's velocity (world space) + angular rates (body space).
    void setVelocity(const RawVec3d& linear, const RawVec3f& angular);
    void step(float dt);
    [[nodiscard]] RawTransform shipTransform() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::game
