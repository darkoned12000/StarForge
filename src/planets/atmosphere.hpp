#pragma once

#include "gpu_resources/buffer.hpp"
#include <cstdint>

namespace starforge::planets
{

// Atmosphere rendering parameters
struct AtmosphereParams
{
    // Planet radius (meters)
    float planetRadius;
    
    // Atmospheric radius (meters) 
    float atmosphereRadius;
    
    // Rayleigh scattering coefficients (RGB)
    float rayleighScatter[3];
    
    // Mie scattering coefficient
    float mieScatter;
    
    // Mie phase function asymmetry parameter
    float mieG;
    
    // Sun direction (normalized)
    float sunDirection[3];
    
    // Sun irradiance (W/m²)  
    float sunIrradiance;
};

// Atmosphere rendering system 
class AtmosphereRenderer
{
public:
    explicit AtmosphereRenderer();
    
    // Initialize atmosphere with parameters
    bool initialize(const AtmosphereParams& params);
    
    // Render atmosphere as a post-process effect
    void render() const;

private:
    AtmosphereParams params_;
    bool initialized_ = false;
};

} // namespace starforge::planets