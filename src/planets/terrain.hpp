#pragma once

#include "math/math.hpp"

#include <cstdint>

namespace starforge::planets
{

// Simple deterministic fractal noise for terrain height.
class TerrainHeightGenerator
{
public:
    explicit TerrainHeightGenerator(std::uint64_t seed = 12345);
    
    // Generate height at a point on the unit sphere.
    double heightAt(const math::Vec3d& sphereDir, double frequency = 1.0) const;
    
    // Generate height with multiple octaves (fractal noise).
    double heightFractal(const math::Vec3d& sphereDir, int octaves = 6, double lacunarity = 2.0, double persistence = 0.5, double baseFrequency = 1.0) const;

private:
    std::uint64_t seed_;
    
    double noise3D(double x, double y, double z) const;
    double hash(std::uint64_t x) const;
};

} // namespace starforge::planets
