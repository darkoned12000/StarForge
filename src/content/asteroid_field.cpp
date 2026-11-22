#include "content/asteroid_field.hpp"

#include "content/hash.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::content
{

namespace
{

// Density modulation: the field is not uniform. A low-frequency value noise
// carves out lanes and clusters so the debris reads as a belt with
// structure rather than as a uniform sprinkle.
float densityAt(std::int64_t cx, std::int64_t cy, std::int64_t cz)
{
    // Sample the noise on a coarse lattice: one sample per 4 cells (1 km).
    // An 8-cell lattice made the clumps 2 km wide, which reads as a hole in
    // the field rather than as a denser knot.
    const auto nx = static_cast<double>(cx) / 4.0;
    const auto ny = static_cast<double>(cy) / 4.0;
    const auto nz = static_cast<double>(cz) / 4.0;

    const auto ix = static_cast<std::int64_t>(std::floor(nx));
    const auto iy = static_cast<std::int64_t>(std::floor(ny));
    const auto iz = static_cast<std::int64_t>(std::floor(nz));
    const float tx = static_cast<float>(nx - static_cast<double>(ix));
    const float ty = static_cast<float>(ny - static_cast<double>(iy));
    const float tz = static_cast<float>(nz - static_cast<double>(iz));
    const float ux = tx * tx * (3.0f - 2.0f * tx);
    const float uy = ty * ty * (3.0f - 2.0f * ty);
    const float uz = tz * tz * (3.0f - 2.0f * tz);

    const auto corner = [&](std::int64_t a, std::int64_t b, std::int64_t c) {
        const auto h = splitMix64(hashCell(a, b, c));
        return static_cast<float>(h >> 40) / 16777216.0f;
    };

    const float c000 = corner(ix, iy, iz);
    const float c100 = corner(ix + 1, iy, iz);
    const float c010 = corner(ix, iy + 1, iz);
    const float c110 = corner(ix + 1, iy + 1, iz);
    const float c001 = corner(ix, iy, iz + 1);
    const float c101 = corner(ix + 1, iy, iz + 1);
    const float c011 = corner(ix, iy + 1, iz + 1);
    const float c111 = corner(ix + 1, iy + 1, iz + 1);

    const float x00 = c000 + (c100 - c000) * ux;
    const float x10 = c010 + (c110 - c010) * ux;
    const float x01 = c001 + (c101 - c001) * ux;
    const float x11 = c011 + (c111 - c011) * ux;
    const float y0 = x00 + (x10 - x00) * uy;
    const float y1 = x01 + (x11 - x01) * uy;
    return y0 + (y1 - y0) * uz;
}

} // namespace

AsteroidField::AsteroidField(Config config) : config_(config)
{
    if (config_.cellSize <= 0.0)
    {
        config_.cellSize = 256.0;
    }
    if (config_.rocksPerCell < 0.0f)
    {
        config_.rocksPerCell = 0.0f;
    }
    if (config_.maxScale < config_.minScale)
    {
        config_.maxScale = config_.minScale;
    }
    if (config_.fadeEnd < config_.fadeStart)
    {
        config_.fadeEnd = config_.fadeStart;
    }
}

std::int64_t AsteroidField::cellIndex(double metres) const noexcept
{
    return static_cast<std::int64_t>(std::floor(metres / config_.cellSize));
}

double AsteroidField::cellOrigin(std::int64_t index) const noexcept
{
    return static_cast<double>(index) * config_.cellSize;
}

double AsteroidField::cellDistanceSquared(double x, double y, double z,
                                           std::int64_t cx, std::int64_t cy,
                                           std::int64_t cz) const noexcept
{
    const double ox = cellOrigin(cx);
    const double oy = cellOrigin(cy);
    const double oz = cellOrigin(cz);
    // Clamp the point into the cell, then measure to the clamped point:
    // that is the distance to the cell's nearest corner.
    const double px = std::clamp(x, ox, ox + config_.cellSize);
    const double py = std::clamp(y, oy, oy + config_.cellSize);
    const double pz = std::clamp(z, oz, oz + config_.cellSize);
    const double dx = x - px;
    const double dy = y - py;
    const double dz = z - pz;
    return dx * dx + dy * dy + dz * dz;
}

void AsteroidField::generateCell(std::int64_t cx, std::int64_t cy, std::int64_t cz,
                                 std::vector<AsteroidSample>& out) const
{
    // Density gate: only genuinely empty cells are skipped. The field has
    // to stay continuous - a lane with no rocks at all reads as a hole in
    // the world when the ship flies through it.
    const float density = densityAt(cx, cy, cz);
    if (density < 0.08f)
    {
        return;
    }

    DeterministicRng rng(contentSeed(config_.worldSeed, cx, cy, cz, 0));
    // Density scales the count: sparse lanes hold one rock, dense knots
    // hold a dozen. The floor keeps every cell populated.
    const int count = static_cast<int>(config_.rocksPerCell *
                                       (0.55f + 2.0f * density));
    if (count <= 0)
    {
        return;
    }

    const double originX = cellOrigin(cx);
    const double originY = cellOrigin(cy);
    const double originZ = cellOrigin(cz);
    const double cell = config_.cellSize;

    for (int i = 0; i < count; ++i)
    {
        AsteroidSample sample;
        sample.x = originX + rng.nextDouble() * cell;
        sample.y = originY + rng.nextDouble() * cell;
        sample.z = originZ + rng.nextDouble() * cell;

        // Power-law-ish size distribution: mostly small pebbles with a few
        // large boulders, like a real debris field.
        const float u = rng.nextFloat();
        const float sizeT = u * u * u;
        sample.scale = config_.minScale +
                       (config_.maxScale - config_.minScale) * sizeT;

        sample.yaw = rng.nextFloat() * 6.28318530718f;
        sample.pitch = rng.nextFloat() * 6.28318530718f;
        sample.roll = rng.nextFloat() * 6.28318530718f;

        // Random tumble axis and a slow spin: the field reads as a debris
        // cloud in motion rather than a frozen snapshot.
        const float az = rng.nextFloat() * 2.0f - 1.0f;
        const float aa = rng.nextFloat() * 6.28318530718f;
        const float as = std::sqrt(std::max(0.0f, 1.0f - az * az));
        sample.spinAxis[0] = as * std::cos(aa);
        sample.spinAxis[1] = az;
        sample.spinAxis[2] = as * std::sin(aa);
        sample.spinRate = (rng.nextFloat() - 0.5f) * 0.35f;

        sample.fadeStart = config_.fadeStart;
        sample.fadeEnd = config_.fadeEnd;

        // Stable per-rock id: derived from the cell and the index inside
        // it, never from the camera, so a rock keeps its identity (and its
        // shape/tint) as the player flies past it.
        const auto h = splitMix64(contentSeed(config_.worldSeed, cx, cy, cz, 1) +
                                   static_cast<std::uint64_t>(i) * 0x9e3779b97f4a7c15ull);
        sample.seed = static_cast<std::uint32_t>(h >> 32);

        out.push_back(sample);
    }
}

} // namespace starforge::content
