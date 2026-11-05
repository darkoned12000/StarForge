#include "planets/terrain.hpp"

#include "jobs/job_scheduler.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::planets
{

namespace
{

[[nodiscard]] inline std::uint64_t mix64(std::uint64_t x) noexcept
{
    x = (x ^ (x >> 33)) * 0xff51afd7ed558ccdULL;
    x = (x ^ (x >> 33)) * 0xc4ceb9fe1a85ec53ULL;
    return x ^ (x >> 33);
}

} // namespace

TerrainHeightGenerator::TerrainHeightGenerator(std::uint64_t seed) : seed_(seed)
{
}

double TerrainHeightGenerator::hash(std::uint64_t x) const noexcept
{
    x ^= seed_;
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111b9ULL;
    constexpr double kInvMaxUint32 = 1.0 / 4294967295.0;
    return static_cast<double>((x ^ (x >> 31)) & 0xFFFFFFFF) * kInvMaxUint32;
}

double TerrainHeightGenerator::noise3D(double x, double y, double z) const
{
    const long long ix = static_cast<long long>(std::floor(x));
    const long long iy = static_cast<long long>(std::floor(y));
    const long long iz = static_cast<long long>(std::floor(z));

    double fx = x - static_cast<double>(ix);
    double fy = y - static_cast<double>(iy);
    double fz = z - static_cast<double>(iz);

    // Quintic smoothstep (C2 continuous) to eliminate lighting creases
    fx = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);
    fy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);
    fz = fz * fz * fz * (fz * (fz * 6.0 - 15.0) + 10.0);

    const std::uint64_t x0 = mix64(static_cast<std::uint64_t>(ix)) * 131542391ULL;
    const std::uint64_t x1 = mix64(static_cast<std::uint64_t>(ix + 1)) * 131542391ULL;
    const std::uint64_t y0 = mix64(static_cast<std::uint64_t>(iy)) * 1000000009ULL;
    const std::uint64_t y1 = mix64(static_cast<std::uint64_t>(iy + 1)) * 1000000009ULL;
    const std::uint64_t z0 = mix64(static_cast<std::uint64_t>(iz)) * 1000000021ULL;
    const std::uint64_t z1 = mix64(static_cast<std::uint64_t>(iz + 1)) * 1000000021ULL;

    const double v000 = hash(x0 + y0 + z0);
    const double v100 = hash(x1 + y0 + z0);
    const double v010 = hash(x0 + y1 + z0);
    const double v110 = hash(x1 + y1 + z0);
    const double v001 = hash(x0 + y0 + z1);
    const double v101 = hash(x1 + y0 + z1);
    const double v011 = hash(x0 + y1 + z1);
    const double v111 = hash(x1 + y1 + z1);

    const double x00 = v000 * (1.0 - fx) + v100 * fx;
    const double x10 = v010 * (1.0 - fx) + v110 * fx;
    const double x01 = v001 * (1.0 - fx) + v101 * fx;
    const double x11 = v011 * (1.0 - fx) + v111 * fx;

    const double y0_val = x00 * (1.0 - fy) + x10 * fy;
    const double y1_val = x01 * (1.0 - fy) + x11 * fy;

    return y0_val * (1.0 - fz) + y1_val * fz;
}

double TerrainHeightGenerator::heightAt(const math::Vec3d& sphereDir, double frequency) const
{
    return noise3D(sphereDir.x * frequency, sphereDir.y * frequency, sphereDir.z * frequency) * 2.0 - 1.0;
}

double TerrainHeightGenerator::heightFractal(
    const math::Vec3d& sphereDir,
    int octaves,
    double lacunarity,
    double persistence,
    double baseFrequency) const
{
    if (octaves <= 0) return 0.0;

    double total = 0.0;
    double amplitude = 1.0;
    double frequency = baseFrequency;
    double maxValue = 0.0;

    for (int i = 0; i < octaves; ++i)
    {
        // Decorrelate octave coordinates to prevent harmonic grid alignment
        const math::Vec3d offsetDir{
            sphereDir.x * frequency + static_cast<double>(i * 17.13),
            sphereDir.y * frequency + static_cast<double>(i * 31.71),
            sphereDir.z * frequency + static_cast<double>(i * 73.37)
        };

        const double n = noise3D(offsetDir.x, offsetDir.y, offsetDir.z) * 2.0 - 1.0;
        total += n * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return (maxValue > 0.0) ? (total / maxValue) : 0.0;
}

void TerrainHeightGenerator::heightFractalBatch(
    std::span<const math::Vec3d> dirs,
    std::span<double> outHeights,
    int octaves,
    double lacunarity,
    double persistence,
    double baseFrequency) const
{
    const std::size_t count = std::min(dirs.size(), outHeights.size());
    if (count == 0) return;

    // Small batch fast-path: evaluate directly on calling thread
    if (count <= 128 || starforge::jobs::globalScheduler().threadCount() <= 1)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            outHeights[i] = heightFractal(dirs[i], octaves, lacunarity, persistence, baseFrequency);
        }
        return;
    }

    constexpr std::size_t kBatchSize = 128;
    const std::size_t batchCount = (count + kBatchSize - 1) / kBatchSize;

    struct BatchCtx
    {
        const TerrainHeightGenerator* gen;
        std::span<const math::Vec3d> dirs;
        std::span<double> outHeights;
        int octaves;
        double lacunarity;
        double persistence;
        double baseFrequency;
    };

    std::vector<BatchCtx> batches(batchCount);
    std::vector<starforge::jobs::TaskHandle> handles;
    handles.reserve(batchCount);

    for (std::size_t b = 0; b < batchCount; ++b)
    {
        const std::size_t start = b * kBatchSize;
        const std::size_t sliceSize = std::min(kBatchSize, count - start);

        batches[b] = BatchCtx{
            .gen = this,
            .dirs = dirs.subspan(start, sliceSize),
            .outHeights = outHeights.subspan(start, sliceSize),
            .octaves = octaves,
            .lacunarity = lacunarity,
            .persistence = persistence,
            .baseFrequency = baseFrequency,
        };

        auto fn = [](void* p) {
            auto* ctx = static_cast<BatchCtx*>(p);
            for (std::size_t i = 0; i < ctx->dirs.size(); ++i)
            {
                ctx->outHeights[i] = ctx->gen->heightFractal(
                    ctx->dirs[i], ctx->octaves, ctx->lacunarity, ctx->persistence, ctx->baseFrequency);
            }
        };

        handles.push_back(starforge::jobs::submit(fn, &batches[b]));
    }

    for (const auto& h : handles)
    {
        starforge::jobs::globalScheduler().wait(h);
    }
}

std::vector<double> TerrainHeightGenerator::heightFractalBatch(
    std::span<const math::Vec3d> dirs,
    int octaves,
    double lacunarity,
    double persistence,
    double baseFrequency) const
{
    std::vector<double> results(dirs.size());
    heightFractalBatch(dirs, results, octaves, lacunarity, persistence, baseFrequency);
    return results;
}

} // namespace starforge::planets
