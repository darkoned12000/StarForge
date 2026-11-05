#pragma once

#include "math/math.hpp"

#include <vector>

namespace starforge::planets
{

struct AtmosphereParameters
{
    double planetRadius = 6371000.0;
    double atmosphereRadius = 6471000.0;
    double rayleighScaleHeight = 8000.0;
    double mieScaleHeight = 1200.0;
    double mieAsymmetry = 0.8;
    math::Vec3d rayleighScattering{5.8e-6, 13.5e-6, 33.1e-6};
    math::Vec3d mieScattering{2.0e-5, 2.0e-5, 2.0e-5};
    math::Vec3d mieAbsorption{4.4e-6, 4.4e-6, 4.4e-6};
    math::Vec3d ozoneAbsorption{0.65e-6, 1.88e-6, 0.085e-6};
    double sunIntensity = 20.0;
};

// Compute single scattering for atmospheric rendering.
math::Vec3d computeSingleScattering(
    const AtmosphereParameters& params,
    const math::Vec3d& rayOrigin,
    const math::Vec3d& rayDir,
    const math::Vec3d& sunDir);

} // namespace starforge::planets
