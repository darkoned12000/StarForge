#include "planets/atmosphere.hpp"

#include <cmath>

namespace starforge::planets
{

math::Vec3d computeSingleScattering(
    const AtmosphereParameters& params,
    const math::Vec3d& rayOrigin,
    const math::Vec3d& rayDir,
    const math::Vec3d& sunDir)
{
    // Simplified single scattering approximation
    double viewHeight = std::sqrt(rayOrigin.x*rayOrigin.x + rayOrigin.y*rayOrigin.y + rayOrigin.z*rayOrigin.z);
    if (viewHeight < params.planetRadius) viewHeight = params.planetRadius;
    
    // Rayleigh phase function
    double mu = rayDir.x * sunDir.x + rayDir.y * sunDir.y + rayDir.z * sunDir.z;
    double rayleighPhase = 0.75 * (1.0 + mu * mu);
    
    // Simple exponential falloff
    double heightFactor = std::exp(-(viewHeight - params.planetRadius) / 8000.0);
    
    math::Vec3d scattering = params.rayleighScattering * rayleighPhase * heightFactor * params.sunIntensity;
    
    return scattering;
}

} // namespace starforge::planets
