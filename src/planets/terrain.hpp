#pragma once

#include "math/math.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace starforge::planets
{

class TerrainHeightGenerator
{
public:
    explicit TerrainHeightGenerator(std::uint64_t seed = 12345);

    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

    [[nodiscard]] double heightAt(const math::Vec3d& sphereDir, double frequency = 1.0) const;

    [[nodiscard]] double heightFractal(
        const math::Vec3d& sphereDir,
        int octaves = 6,
        double lacunarity = 2.0,
        double persistence = 0.5,
        double baseFrequency = 1.0) const;

    void heightFractalBatch(
        std::span<const math::Vec3d> dirs,
        std::span<double> outHeights,
        int octaves = 6,
        double lacunarity = 2.0,
        double persistence = 0.5,
        double baseFrequency = 1.0) const;

    [[nodiscard]] std::vector<double> heightFractalBatch(
        std::span<const math::Vec3d> dirs,
        int octaves = 6,
        double lacunarity = 2.0,
        double persistence = 0.5,
        double baseFrequency = 1.0) const;

private:
    std::uint64_t seed_;

    [[nodiscard]] double noise3D(double x, double y, double z) const;
    [[nodiscard]] double hash(std::uint64_t x) const noexcept;
};

} // namespace starforge::planets
