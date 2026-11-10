#pragma once

// Bridge between the game loop (public API) and the ECS world.
// Plain data only: no TU that calls the bridge ever includes both the
// public math header and the internal GLM-based math aliases (the same
// rule the public API bridges use).

#include <memory>
#include <vector>

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
    // Sphere push-out: if the ship overlaps an asteroid, slide it out
    // to the surface and log it. Returns true on contact.
    [[nodiscard]] bool collideShip();

    // Procedural streaming (M15.0.3): advance the cell traversal from
    // the ship position, instantiate newly loaded chunks into ECS and
    // unmount evicted ones, respecting the per-frame upload budget.
    // Returns the number of chunks loaded this call (for logging).
    [[nodiscard]] int updateStreaming();
    // Camera-cell coordinates for HUD/debug logs.
    [[nodiscard]] RawVec3d shipCell() const;
    // Asteroids within `radiusMetres` of the ship, ship-relative
    // positions (metres) with orientation + scale, for Toon rendering.
    [[nodiscard]] std::vector<RawTransform> visibleAsteroids(double radiusMetres) const;
    [[nodiscard]] std::size_t mountedAsteroidCount() const;
    [[nodiscard]] std::size_t streamedChunkCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::game
