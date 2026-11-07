#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace starforge::planets
{

// Atmosphere rendering parameters
struct AtmosphereParams
{
    // Planet radius (meters)
    float planetRadius = 6371000.0f;

    // Atmospheric radius (meters)
    float atmosphereRadius = 6471000.0f;

    // Rayleigh scattering coefficients (RGB)
    float rayleighScatter[3] = {5.8e-6f, 13.5e-6f, 33.1e-6f};

    // Mie scattering coefficient
    float mieScatter = 2.0e-5f;

    // Mie phase function asymmetry parameter
    float mieG = 0.76f;

    // Sun direction (normalized, planet frame, zenith = +Y)
    float sunDirection[3] = {0.0f, 0.0f, -1.0f};

    // Sun irradiance (W/m²)
    float sunIrradiance = 1361.0f;
};

// Precomputed 2D scattering LUT over (muView, muSun), where muX is the
// cosine of the zenith angle. Row-major data[height][width], RGB.
struct AtmosphereLut
{
    std::uint32_t width = 128;
    std::uint32_t height = 64;
    std::vector<std::array<float, 3>> data;
};

// Generate the scattering LUT by integrating single scattering along
// view rays through a spherical shell atmosphere. When `parallel` is
// true, row batches run on the global job scheduler; output is
// bit-identical either way (each texel is an independent pure function).
[[nodiscard]] AtmosphereLut generateAtmosphereLut(
    const AtmosphereParams& params, bool parallel = true);

// Atmosphere rendering state: owns parameters + precomputed LUT.
// GPU shell rendering consumes `lut()` (see TODO 7.4b follow-ups).
class AtmosphereRenderer
{
public:
    explicit AtmosphereRenderer();

    // Initialize atmosphere with parameters and generate the LUT.
    bool initialize(const AtmosphereParams& params, bool parallel = true);

    [[nodiscard]] const AtmosphereLut& lut() const noexcept { return lut_; }
    [[nodiscard]] const AtmosphereParams& params() const noexcept { return params_; }

    // Render atmosphere as a post-process effect (rendering lives in the
    // GPU shell pass; this CPU-side method is a documented no-op).
    void render() const;

private:
    AtmosphereParams params_;
    AtmosphereLut lut_;
    bool initialized_ = false;
};

} // namespace starforge::planets
