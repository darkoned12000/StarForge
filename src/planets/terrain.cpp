#include "planets/terrain.hpp"

#include <cmath>

namespace starforge::planets
{

TerrainHeightGenerator::TerrainHeightGenerator(std::uint64_t seed) : seed_(seed)
{
}

double TerrainHeightGenerator::hash(std::uint64_t x) const
{
    x ^= seed_;
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111b9ULL;
    return static_cast<double>((x ^ (x >> 31)) & 0xFFFFFFFF) / static_cast<double>(0xFFFFFFFF);
}

double TerrainHeightGenerator::noise3D(double x, double y, double z) const
{
    // Simple value noise
    long long ix = static_cast<long long>(std::floor(x));
    long long iy = static_cast<long long>(std::floor(y));
    long long iz = static_cast<long long>(std::floor(z));
    
    double fx = x - static_cast<double>(ix);
    double fy = y - static_cast<double>(iy);
    double fz = z - static_cast<double>(iz);
    
    // Smoothstep
    fx = fx * fx * (3.0 - 2.0 * fx);
    fy = fy * fy * (3.0 - 2.0 * fy);
    fz = fz * fz * (3.0 - 2.0 * fz);
    
    auto mix64 = [&](std::uint64_t x) -> std::uint64_t {
        x = (x ^ (x >> 33)) * 0xff51afd7ed558ccdULL;
        x = (x ^ (x >> 33)) * 0xc4ceb9fe1a85ec53ULL;
        return x ^ (x >> 33);
    };

    std::uint64_t xi0 = mix64(static_cast<std::uint64_t>(ix));
    std::uint64_t xi1 = mix64(static_cast<std::uint64_t>(ix + 1));
    std::uint64_t yi0 = mix64(static_cast<std::uint64_t>(iy));
    std::uint64_t yi1 = mix64(static_cast<std::uint64_t>(iy + 1));
    std::uint64_t zi0 = mix64(static_cast<std::uint64_t>(iz));
    std::uint64_t zi1 = mix64(static_cast<std::uint64_t>(iz + 1));

    auto h = [&](std::uint64_t xi, std::uint64_t yi, std::uint64_t zi) {
        std::uint64_t n = xi * 131542391ULL + yi * 1000000009ULL + zi * 1000000021ULL;
        return hash(n);
    };

    double v000 = h(xi0, yi0, zi0);
    double v100 = h(xi1, yi0, zi0);
    double v010 = h(xi0, yi1, zi0);
    double v110 = h(xi1, yi1, zi0);
    double v001 = h(xi0, yi0, zi1);
    double v101 = h(xi1, yi0, zi1);
    double v011 = h(xi0, yi1, zi1);
    double v111 = h(xi1, yi1, zi1);
    
    
    double x00 = v000 * (1.0 - fx) + v100 * fx;
    double x10 = v010 * (1.0 - fx) + v110 * fx;
    double x01 = v001 * (1.0 - fx) + v101 * fx;
    double x11 = v011 * (1.0 - fx) + v111 * fx;
    
    double y0 = x00 * (1.0 - fy) + x10 * fy;
    double y1 = x01 * (1.0 - fy) + x11 * fy;
    
    return y0 * (1.0 - fz) + y1 * fz;
}

double TerrainHeightGenerator::heightAt(const math::Vec3d& sphereDir, double frequency) const
{
    // Map sphere direction to 3D noise coordinates
    double x = sphereDir.x * frequency;
    double y = sphereDir.y * frequency;
    double z = sphereDir.z * frequency;
    
    double n = noise3D(x, y, z);
    // Return height in [-1, 1]
    return n * 2.0 - 1.0;
}

double TerrainHeightGenerator::heightFractal(const math::Vec3d& sphereDir, int octaves, double lacunarity, double persistence, double baseFrequency) const
{
    if (octaves <= 0) return 0.0;
    double total = 0.0;
    double amplitude = 1.0;
    double frequency = baseFrequency;
    double maxValue = 0.0;
    
    for (int i = 0; i < octaves; ++i)
    {
        total += heightAt(sphereDir, frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    
    if (maxValue == 0.0) return 0.0;
    return total / maxValue;
}

} // namespace starforge::planets
