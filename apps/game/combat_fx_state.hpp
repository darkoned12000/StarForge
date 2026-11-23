#pragma once

// M17.5 combat effect state. Pure CPU, deterministic, and bounded by fixed
// pools: no allocation on the render path, no per-frame resource churn and
// no effect is ever persisted (M18). Effects are spawned from GameEvents,
// never by polling gameplay state.
#include "content/hash.hpp"

#include "starforge/combat_fx.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace starforge::game
{

class CombatFxState
{
public:
    static constexpr std::size_t kMaxBolts = 256;
    static constexpr std::size_t kMaxParticles = 2048;
    static constexpr std::size_t kMaxExplosions = 8;
    static constexpr std::size_t kMaxInstances = 4096;

    explicit CombatFxState(std::uint64_t seed = 0x5EED175u);

    void setSeed(std::uint64_t seed) noexcept;

    // Event handlers. All spawns clamp silently when a pool is full so pool
    // exhaustion degrades to "fewer sparks", never a crash or a growth.
    void onWeaponFired(const starforge::math::Vec3f& origin,
                       const starforge::math::Vec3f& direction,
                       const starforge::math::Vec4f& color,
                       float radius, float length);
    void onImpact(const starforge::math::Vec3f& position,
                  const starforge::math::Vec4f& color, bool shieldHit);
    void onShieldHit(const starforge::math::Vec3f& position,
                     const starforge::math::Vec4f& color, float strength);
    void onShipDestroyed(const starforge::math::Vec3f& position, float size,
                         const starforge::math::Vec4f& color,
                         std::span<const starforge::math::Vec3f> debrisOrigins,
                         bool large);

    // Engine plumes are continuous ship state rather than events; they are
    // rebuilt from the current throttle/boost each frame into their own
    // bounded slot.
    void setEngines(const starforge::math::Vec3f& nozzleLeft,
                    const starforge::math::Vec3f& nozzleRight,
                    const starforge::math::Vec3f& exhaustDirection,
                    const starforge::math::Vec4f& color,
                    float throttle, bool boost, float time);

    void update(float dt);
    void clear() noexcept;

    [[nodiscard]] std::span<const starforge::CombatFx::Instance> instances() const noexcept
    {
        return {instances_.data(), instanceCount_};
    }
    [[nodiscard]] std::size_t boltCount() const noexcept { return boltCount_; }
    [[nodiscard]] std::size_t particleCount() const noexcept { return particleCount_; }
    [[nodiscard]] std::size_t explosionCount() const noexcept { return explosionCount_; }
    [[nodiscard]] bool full() const noexcept { return instanceCount_ >= kMaxInstances; }

    // Camera shake displacement, scaled by explosion size and distance.
    [[nodiscard]] starforge::math::Vec3f cameraShake(const starforge::math::Vec3f& cameraPosition) const noexcept;

private:
    struct Bolt
    {
        starforge::math::Vec3f position{};
        starforge::math::Vec3f direction{};
        starforge::math::Vec4f color{};
        float age = 0.0f;
        float radius = 0.0f;
        float length = 0.0f;
    };
    struct Particle
    {
        starforge::math::Vec3f position{};
        starforge::math::Vec3f velocity{};
        starforge::math::Vec4f color{};
        float age = 0.0f;
        float life = 1.0f;
        float size = 1.0f;
        float seed = 0.0f;
        // Tumbling hull panel: drawn as a short oriented beam and carried
        // by the particle pool so it drifts and fades like every other
        // transient instead of being wiped by the next update().
        bool beam = false;
        starforge::math::Vec3f axis{0.0f, 0.0f, 1.0f};
    };
    struct Explosion
    {
        starforge::math::Vec3f position{};
        starforge::math::Vec4f color{};
        float age = 0.0f;
        float size = 1.0f;
        float seed = 0.0f;
        bool large = false;
    };
    struct Ripple
    {
        starforge::math::Vec3f position{};
        starforge::math::Vec4f color{};
        float age = 0.0f;
        float radius = 1.0f;
    };

    void push(const starforge::CombatFx::Instance& instance) noexcept;
    void spawnExplosion(const starforge::math::Vec3f& position, float size,
                        const starforge::math::Vec4f& color, float seed, bool large);
    void emitExplosion(const Explosion& explosion) noexcept;

    std::array<Bolt, kMaxBolts> bolts_{};
    std::size_t boltCount_ = 0;
    std::array<Particle, kMaxParticles> particles_{};
    std::size_t particleCount_ = 0;
    std::array<Explosion, kMaxExplosions> explosions_{};
    std::size_t explosionCount_ = 0;
    // Shield ripples are thin shells hugging the hull, never fireballs: a
    // big additive billboard at the ship position washes out the frame.
    std::array<Ripple, 8> ripples_{};
    std::size_t rippleCount_ = 0;
    std::array<starforge::CombatFx::Instance, kMaxInstances> instances_{};
    std::size_t instanceCount_ = 0;

    // Engine plume slots are rebuilt each frame and are not part of the
    // persistent pools.
    std::array<starforge::CombatFx::Instance, 2> engines_{};
    bool enginesActive_ = false;

    content::DeterministicRng rng_;
    float time_ = 0.0f;
};

} // namespace starforge::game
