#include "combat_fx_state.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::game
{

namespace
{
using starforge::math::Vec3f;
using starforge::math::Vec4f;
using Kind = starforge::CombatFx::Kind;

constexpr float kBoltSpeed = 600.0f;
constexpr float kBoltLife = 1.6f;

[[nodiscard]] inline float length(const Vec3f& v) noexcept
{
    return std::sqrt(v.x() * v.x() + v.y() * v.y() + v.z() * v.z());
}

[[nodiscard]] inline Vec3f normalized(const Vec3f& v) noexcept
{
    const float l = length(v);
    return l > 1e-5f ? Vec3f{v.x() / l, v.y() / l, v.z() / l} : Vec3f{0.0f, 0.0f, -1.0f};
}
} // namespace

CombatFxState::CombatFxState(std::uint64_t seed) : rng_(seed) {}

void CombatFxState::setSeed(std::uint64_t seed) noexcept
{
    rng_ = content::DeterministicRng(seed);
    time_ = 0.0f;
}

void CombatFxState::clear() noexcept
{
    boltCount_ = 0;
    particleCount_ = 0;
    explosionCount_ = 0;
    rippleCount_ = 0;
    instanceCount_ = 0;
    time_ = 0.0f;
    enginesActive_ = false;
    enginesActive_ = false;
}

void CombatFxState::push(const starforge::CombatFx::Instance& instance) noexcept
{
    if (instanceCount_ >= kMaxInstances) return;
    instances_[instanceCount_++] = instance;
}

void CombatFxState::onWeaponFired(const Vec3f& origin, const Vec3f& direction,
                                  const Vec4f& color, float radius, float lengthMetres)
{
    if (boltCount_ >= kMaxBolts) return;
    const auto axis = normalized(direction);
    bolts_[boltCount_++] = Bolt{origin, axis, color, 0.0f,
                                std::max(0.05f, radius), std::max(0.2f, lengthMetres)};
    // One tight muzzle flash per barrel, stored in the particle pool so it
    // survives update() like every other transient. Firing a burst must not
    // spray sparks in all directions - that reads as a firework, not a gun.
    if (particleCount_ < kMaxParticles)
        particles_[particleCount_++] = Particle{
            origin + axis * radius, {}, color, 0.0f, 0.05f,
            radius * 1.6f, rng_.nextFloat() * 97.0f, false, {}};

}

void CombatFxState::onImpact(const Vec3f& position, const Vec4f& color, bool shieldHit)
{
    // Small, directed clusters: with a dense asteroid field a shot lands
    // every frame, so 10 wide sparks per hit became a wall of confetti.
    const int count = shieldHit ? 3 : 2;
    for (int i = 0; i < count; ++i)
    {
        if (particleCount_ >= kMaxParticles) break;
        const Vec3f dir{rng_.nextFloat() * 2.0f - 1.0f,
                        rng_.nextFloat() * 2.0f - 1.0f,
                        rng_.nextFloat() * 2.0f - 1.0f};
        const float speed = (shieldHit ? 9.0f : 12.0f) * (0.4f + rng_.nextFloat() * 0.6f);
        const auto velocity = normalized(dir) * speed;
        particles_[particleCount_++] = Particle{
            position, velocity, color, 0.0f, 0.14f + rng_.nextFloat() * 0.16f,
            0.06f + rng_.nextFloat() * 0.08f, rng_.nextFloat() * 97.0f, false, {}};
    }
}

void CombatFxState::onShieldHit(const Vec3f& position, const Vec4f& color, float strength)
{
    onImpact(position, color, true);
    // A thin expanding shield ripple hugging the hull. Never a fireball:
    // a large additive billboard at the camera-relative origin covers the
    // whole frame and washes out the scene.
    if (rippleCount_ < ripples_.size())
    {
        ripples_[rippleCount_++] = Ripple{
            position, color, 0.0f, std::clamp(strength * 2.2f, 1.5f, 4.0f)};
    }
}

void CombatFxState::spawnExplosion(const Vec3f& position, float size,
                                   const Vec4f& color, float seed, bool large)
{
    if (explosionCount_ >= kMaxExplosions) return;
    explosions_[explosionCount_++] = Explosion{position, color, 0.0f, size, seed, large};
}

void CombatFxState::onShipDestroyed(const Vec3f& position, float size,
                                    const Vec4f& color,
                                    std::span<const Vec3f> debrisOrigins,
                                    bool large)
{
    const float seed = rng_.nextFloat() * 97.0f;
    spawnExplosion(position, size, color, seed, large);
    // Debris pushed out from the ship's own parts: a burst of small hot
    // fragments plus a few larger tumbling hull pieces with ember trails.
    const std::size_t count = std::min(debrisOrigins.size() * 3, kMaxParticles / 4);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (particleCount_ >= kMaxParticles) break;
        const Vec3f dir{rng_.nextFloat() * 2.0f - 1.0f,
                        rng_.nextFloat() * 2.0f - 1.0f,
                        rng_.nextFloat() * 2.0f - 1.0f};
        const float speed = (large ? 22.0f : 13.0f) * (0.35f + rng_.nextFloat() * 1.1f);
        const auto origin = debrisOrigins[i % debrisOrigins.size()] +
                            Vec3f{(rng_.nextFloat() - 0.5f) * size * 0.4f,
                                  (rng_.nextFloat() - 0.5f) * size * 0.4f,
                                  (rng_.nextFloat() - 0.5f) * size * 0.4f};
        // Debris must be a few pixels across at combat range (~260 m), so
        // sub-metre sparks are invisible. These are metre-scale fragments.
        particles_[particleCount_++] = Particle{
            origin, normalized(dir) * speed,
            Vec4f{1.0f, 0.55f, 0.18f, 2.4f}, 0.0f, 1.4f + rng_.nextFloat() * 1.1f,
            0.55f + rng_.nextFloat() * 0.65f, seed + static_cast<float>(i), false, {}};
        // A few larger tumbling panels: oriented beams carried by the
        // particle pool, so they drift and fade instead of vanishing after
        // a single frame.
        if (i % 4 == 0)
        {
            const float axisX = rng_.nextFloat() * 2.0f - 1.0f;
            const float axisY = rng_.nextFloat() * 2.0f - 1.0f;
            const float axisZ = rng_.nextFloat() * 2.0f - 1.0f;
            if (particleCount_ < kMaxParticles)
                particles_[particleCount_++] = Particle{
                    origin, normalized(Vec3f{axisX, axisY, axisZ}) * speed * 0.6f,
                    Vec4f{1.0f, 0.72f, 0.35f, 2.6f}, 0.0f,
                    1.2f + rng_.nextFloat() * 0.8f,
                    0.9f + rng_.nextFloat() * 1.1f,
                    seed + static_cast<float>(i) * 3.0f, true,
                    Vec3f{axisX, axisY, axisZ}};
        }
    }
    // Large ships and stations chain several explosions along the hull.
    if (large)
    {
        for (int i = 1; i <= 3; ++i)
        {
            const Vec3f offset{(rng_.nextFloat() - 0.5f) * size,
                               (rng_.nextFloat() - 0.5f) * size,
                               (rng_.nextFloat() - 0.5f) * size};
            spawnExplosion(position + offset, size * (0.55f + rng_.nextFloat() * 0.3f),
                           color, seed + static_cast<float>(i) * 13.0f, false);
        }
    }
}

void CombatFxState::setEngines(const Vec3f& nozzleLeft, const Vec3f& nozzleRight,
                               const Vec3f& exhaustDirection, const Vec4f& color,
                               float throttle, bool boost, float time)
{
    enginesActive_ = throttle > 0.01f;
    if (!enginesActive_) return;
    const float power = std::clamp(throttle, 0.0f, 1.0f);
    // `throttle` is already boost-scaled by the caller; applying another
    // boost factor here double-counts and clips above ~53% throttle.
    (void)boost;
    // The chase camera looks straight down the exhaust, so the flame is
    // a compact glow at the nozzle rather than a long world-space trail.
    const float plumeRadius = 0.38f + power * 0.30f;
    const float intensity = 0.9f + power * 0.8f;
    const Vec3f axis = normalized(exhaustDirection);
    const std::array<Vec3f, 2> nozzles{nozzleLeft, nozzleRight};
    for (std::size_t i = 0; i < 2; ++i)
    {
        engines_[i] = starforge::CombatFx::Instance{
            .position = nozzles[i] + axis * (plumeRadius * 0.35f),
            .radius = plumeRadius,
            .direction = {axis.x(), axis.y(), axis.z()},
            .kind = Kind::Plume,
            .color = {color.x(), color.y(), color.z(), intensity},
            .age = time,
            .seed = static_cast<float>(i) * 17.0f + 3.0f,
            .aspect = 1.6f + power * 0.3f, // vertical stretch: short trailing flame
            .fade = 1.0f,
        };
    }
}

void CombatFxState::emitExplosion(const Explosion& explosion) noexcept
{
    const float t = explosion.age;
    const float s = explosion.size;
    // 1-2 frame white flash for bloom.
    if (t < 0.06f)
    {
        push({.position = explosion.position, .radius = s * 1.6f,
              .color = {3.5f, 3.2f, 2.6f, 2.0f}, .kind = Kind::Fireball,
              .age = t, .seed = explosion.seed, .fade = 1.0f - t / 0.06f});
    }
    // Fireball with noise dissolve, fading over ~1 s.
    if (t < 1.0f)
    {
        const float phase = t / 1.0f;
        push({.position = explosion.position, .radius = s * (0.6f + phase * 1.5f),
              .color = explosion.color, .kind = Kind::Fireball,
              .age = t, .seed = explosion.seed, .fade = 1.0f - phase});
    }
    // Fast shockwave ring.
    if (t < 0.45f)
    {
        const float phase = t / 0.45f;
        push({.position = explosion.position, .radius = s * (0.5f + phase * 3.0f),
              .color = {1.0f, 0.75f, 0.45f, 1.6f}, .kind = Kind::Ring,
              .age = t, .seed = explosion.seed, .fade = 1.0f - phase});
    }
    // Lingering smoke.
    if (t > 0.12f && t < 1.8f)
    {
        const float phase = (t - 0.12f) / 1.68f;
        push({.position = explosion.position + Vec3f{0.0f, phase * s * 0.6f, 0.0f},
              .radius = s * (0.8f + phase * 1.8f),
              .color = {0.16f, 0.15f, 0.18f, 0.7f}, .kind = Kind::Spark,
              .age = t, .seed = explosion.seed + 5.0f, .fade = (1.0f - phase) * 0.7f});
    }
}

void CombatFxState::update(float dt)
{
    time_ += dt;
    instanceCount_ = 0;

    // Bolts: move, age and draw as bright capsules.
    std::size_t write = 0;
    for (std::size_t i = 0; i < boltCount_; ++i)
    {
        auto& bolt = bolts_[i];
        bolt.age += dt;
        if (bolt.age >= kBoltLife) continue;
        bolt.position = bolt.position + Vec3f{bolt.direction.x(), bolt.direction.y(),
                                              bolt.direction.z()} * (kBoltSpeed * dt);
        const float fade = 1.0f - bolt.age / kBoltLife;
        push({.position = bolt.position,
              .radius = bolt.length,
              .direction = {bolt.direction.x(), bolt.direction.y(),
                            bolt.direction.z()},
              .kind = Kind::Beam,
              .color = {bolt.color.x(), bolt.color.y(), bolt.color.z(),
                        bolt.color.w() * fade},
              .age = bolt.age,
              .seed = 0.0f,
              .aspect = 1.0f,
              .fade = fade});
        if (write != i) bolts_[write] = bolt;
        ++write;
    }
    boltCount_ = write;

    // Particles: sparks and smoke with gravity-free inertial drift.
    write = 0;
    for (std::size_t i = 0; i < particleCount_; ++i)
    {
        auto& p = particles_[i];
        p.age += dt;
        if (p.age >= p.life) continue;
        p.position = p.position + p.velocity * dt;
        p.velocity = p.velocity * (1.0f - std::min(1.0f, dt * 2.5f));
        const float fade = 1.0f - p.age / p.life;
        push({.position = p.position, .radius = p.size,
              .direction = p.beam ? p.axis : Vec3f{0.0f, 0.0f, 0.0f},
              .color = {p.color.x(), p.color.y(), p.color.z(), p.color.w() * fade},
              .kind = p.beam ? Kind::Beam : Kind::Spark,
              .age = p.age, .seed = p.seed, .fade = fade});
        if (write != i) particles_[write] = p;
        ++write;
    }
    particleCount_ = write;

    // Explosions: layered by age, then retired.
    write = 0;
    for (std::size_t i = 0; i < explosionCount_; ++i)
    {
        auto& e = explosions_[i];
        e.age += dt;
        const float maxAge = e.large ? 3.0f : 1.8f;
        if (e.age >= maxAge) continue;
        emitExplosion(e);
        if (write != i) explosions_[write] = e;
        ++write;
    }
    explosionCount_ = write;

    // Shield ripples: thin luminous annuli decaying over ~0.4 s.
    write = 0;
    for (std::size_t i = 0; i < rippleCount_; ++i)
    {
        auto& ripple = ripples_[i];
        ripple.age += dt;
        if (ripple.age >= 0.4f) continue;
        const float phase = ripple.age / 0.4f;
        push({.position = ripple.position,
              .radius = ripple.radius * (0.65f + phase * 1.2f),
              .color = {ripple.color.x(), ripple.color.y(), ripple.color.z(),
                        ripple.color.w() * 0.7f},
              .kind = Kind::Ring,
              .age = ripple.age,
              .seed = phase * 13.0f,
              .fade = 1.0f - phase});
        if (write != i) ripples_[write] = ripple;
        ++write;
    }
    rippleCount_ = write;

    // Emit plume and hot core together from the same stored engine state,
    // so neither leads the other by a frame during fast rotation.
    if (enginesActive_)
    {
        for (std::size_t i = 0; i < engines_.size(); ++i)
        {
            const auto& engine = engines_[i];
            push(engine);
            push({.position = engine.position,
                  .radius = engine.radius * 0.28f,
                  .color = {0.6f, 0.9f, 1.0f, 1.5f + engine.color.w() * 0.25f},
                  .kind = Kind::Spark,
                  .age = engine.age,
                  .seed = static_cast<float>(i) * 31.0f + 9.0f,
                  .fade = engine.fade});
        }
    }
}

Vec3f CombatFxState::cameraShake(const Vec3f& cameraPosition) const noexcept
{
    float shake = 0.0f;
    for (std::size_t i = 0; i < explosionCount_; ++i)
    {
        const auto& e = explosions_[i];
        const float distance = length(Vec3f{e.position.x() - cameraPosition.x(),
                                            e.position.y() - cameraPosition.y(),
                                            e.position.z() - cameraPosition.z()});
        const float falloff = 1.0f / (1.0f + distance * 0.01f);
        shake += e.size * falloff * std::exp(-e.age * 3.0f);
    }
    shake = std::min(shake, 1.5f);
    const float t = time_ * 47.0f;
    return Vec3f{std::sin(t) * shake * 0.12f,
                 std::cos(t * 1.37f) * shake * 0.12f,
                 std::sin(t * 0.73f) * shake * 0.06f};
}

} // namespace starforge::game
