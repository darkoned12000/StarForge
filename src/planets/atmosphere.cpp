#include "planets/atmosphere.hpp"

#include "jobs/job_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace starforge::planets
{

namespace
{

// Separate scale heights: Rayleigh molecules ~8 km, Mie aerosols ~1.2 km
// (Earth-like). A single shared height would smear haze into the upper
// stratosphere.
constexpr double kRayleighScaleHeight = 8000.0;
constexpr double kMieScaleHeight = 1200.0;

double densityAt(double height, double h)
{
    return std::exp(-height / h);
}

// Physical Rayleigh phase function.
double rayleighPhase(double cosTheta)
{
    return (3.0 / (16.0 * std::numbers::pi)) * (1.0 + cosTheta * cosTheta);
}

// Normalized Henyey-Greenstein Mie phase function.
double miePhaseHG(double cosTheta, double g)
{
    const double g2 = g * g;
    const double denom = std::pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return denom > 0.0 ? (1.0 / (4.0 * std::numbers::pi)) * (1.0 - g2) / denom : 0.0;
}

// Forward distance from point P along unit dir to the shell sphere
// (radius Rout) or the planet surface (radius R). Returns max distance.
double rayExitDistance(const std::array<double, 3>& p, const double dir[3], double radius, double maxDist)
{
    // |P + t*dir| = radius -> t = -(P.dir) + sqrt((P.dir)^2 - (|P|^2 - r^2))
    const double pd = p[0] * dir[0] + p[1] * dir[1] + p[2] * dir[2];
    const double p2 = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
    const double disc = pd * pd - (p2 - radius * radius);
    if (disc < 0.0)
    {
        return 0.0;
    }
    const double t = -pd + std::sqrt(disc);
    return t > 0.0 ? std::min(t, maxDist) : 0.0;
}

// Single-scattering march for one (muView, muSun) texel. Observer sits
// on the surface at (0, R, 0); view and sun rays lie in the XY plane.
// Single-scattering march for one (muView, muSun) texel. Observer sits
// on the surface at (0, R, 0); view and sun rays lie in the XY plane
// (2D LUT: azimuth difference is assumed 0, documented limitation).
std::array<float, 3> scatterForTexel(const AtmosphereParams& params, double muView, double muSun)
{
    // Downward view rays hit terrain immediately: no atmosphere path.
    if (muView < 0.0)
    {
        return {0.0f, 0.0f, 0.0f};
    }

    const double r = params.planetRadius;
    const double rOut = params.atmosphereRadius;

    const double viewDir[3] = {std::sqrt(std::max(0.0, 1.0 - muView * muView)), muView, 0.0};
    const double sunDir[3] = {std::sqrt(std::max(0.0, 1.0 - muSun * muSun)), muSun, 0.0};
    const double cosTheta =
        viewDir[0] * sunDir[0] + viewDir[1] * sunDir[1] + viewDir[2] * sunDir[2];

    const std::array<double, 3> origin{0.0, r, 0.0};
    const double pathLen = rayExitDistance(origin, viewDir, rOut, 1.0e7);
    if (pathLen <= 0.0)
    {
        return {0.0f, 0.0f, 0.0f};
    }

    const double pRayleigh = rayleighPhase(cosTheta);
    const double pMie = miePhaseHG(cosTheta, params.mieG);

    constexpr double kViewSteps = 32.0;
    constexpr double kSunSteps = 16.0;
    const double dt = pathLen / kViewSteps;

    double scatter[3] = {0.0, 0.0, 0.0};
    double viewOpticalR = 0.0; // View-path optical depth (out-scattering).
    double viewOpticalM = 0.0;

    for (double s = 0.5; s < kViewSteps; s += 1.0)
    {
        const double t = s * dt;
        const double px = origin[0] + t * viewDir[0];
        const double py = origin[1] + t * viewDir[1];
        const double pz = origin[2] + t * viewDir[2];
        const double height = std::sqrt(px * px + py * py + pz * pz) - r;
        if (height < 0.0)
        {
            break; // Ray terminated against terrain.
        }
        const double dRayleigh = densityAt(height, kRayleighScaleHeight) * dt;
        const double dMie = densityAt(height, kMieScaleHeight) * dt;
        viewOpticalR += dRayleigh;
        viewOpticalM += dMie;

        // Sun transmittance: integrate densities from here toward the sun.
        const double sunLen = rayExitDistance(std::array<double, 3>{px, py, pz}, sunDir, rOut, 1.0e7);
        const double sunDt = sunLen / kSunSteps;
        double sunOpticalR = 0.0;
        double sunOpticalM = 0.0;
        bool sunBlocked = false;
        for (double k = 0.5; k < kSunSteps; k += 1.0)
        {
            const double ts = k * sunDt;
            const double qx = px + ts * sunDir[0];
            const double qy = py + ts * sunDir[1];
            const double qz = pz + ts * sunDir[2];
            const double qHeight = std::sqrt(qx * qx + qy * qy + qz * qz) - r;
            if (qHeight < 0.0)
            {
                sunBlocked = true;
                break;
            }
            sunOpticalR += densityAt(qHeight, kRayleighScaleHeight) * sunDt;
            sunOpticalM += densityAt(qHeight, kMieScaleHeight) * sunDt;
        }
        if (sunBlocked)
        {
            continue;
        }

        // Wavelength-dependent transmittance: per-channel Rayleigh
        // extinction (beta_R ~ lambda^-4) plus Mie, over BOTH the view
        // segment (out-scattering) and the sun segment.
        for (int c = 0; c < 3; ++c)
        {
            const double betaR = static_cast<double>(params.rayleighScatter[c]);
            const double betaM = static_cast<double>(params.mieScatter);
            const double tau = betaR * (viewOpticalR + sunOpticalR) + betaM * (viewOpticalM + sunOpticalM);
            const double transmittance = std::exp(-tau);
            scatter[c] += (betaR * pRayleigh * dRayleigh + betaM * pMie * dMie) * transmittance;
        }
    }

    // Absolute exposure is folded into the display normalization in
    // generateAtmosphereLut (LUTs are stored normalized to [0, 1]);
    // sunIrradiance scales the raw integrals before normalization so
    // relative sun brightness is preserved, and renderers apply their
    // own exposure when sampling.
    const double exposure = static_cast<double>(params.sunIrradiance) / 1361.0;
    return {
        static_cast<float>(scatter[0] * exposure),
        static_cast<float>(scatter[1] * exposure),
        static_cast<float>(scatter[2] * exposure),
    };
}

struct LutBatch
{
    const AtmosphereParams* params = nullptr;
    AtmosphereLut* lut = nullptr;
    std::uint32_t rowBegin = 0;
    std::uint32_t rowEnd = 0;
};

void lutBatchTrampoline(void* userData)
{
    auto* batch = static_cast<LutBatch*>(userData);
    const std::uint32_t width = batch->lut->width;
    const std::uint32_t height = batch->lut->height;
    for (std::uint32_t y = batch->rowBegin; y < batch->rowEnd; ++y)
    {
        const double muSun = height <= 1
            ? 0.0
            : (2.0 * static_cast<double>(y) / static_cast<double>(height - 1)) - 1.0;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const double muView = width <= 1
                ? 0.0
                : (2.0 * static_cast<double>(x) / static_cast<double>(width - 1)) - 1.0;
            batch->lut->data[static_cast<std::size_t>(y) * width + x] =
                scatterForTexel(*batch->params, muView, muSun);
        }
    }
}

} // namespace

AtmosphereLut generateAtmosphereLut(const AtmosphereParams& params, bool parallel)
{
    AtmosphereLut lut;
    lut.width = 128;
    lut.height = 64;
    lut.data.resize(static_cast<std::size_t>(lut.width) * lut.height);

    constexpr std::uint32_t kRowsPerBatch = 4;
    const std::uint32_t batchCount = (lut.height + kRowsPerBatch - 1) / kRowsPerBatch;

    std::vector<LutBatch> batches(batchCount);
    for (std::uint32_t b = 0; b < batchCount; ++b)
    {
        const std::uint32_t begin = b * kRowsPerBatch;
        batches[b] = LutBatch{&params, &lut, begin, std::min(begin + kRowsPerBatch, lut.height)};
    }

    if (parallel && batchCount > 1)
    {
        std::vector<jobs::TaskHandle> handles;
        handles.reserve(batchCount);
        for (auto& batch : batches)
        {
            handles.push_back(jobs::submit(&lutBatchTrampoline, &batch));
        }
        jobs::globalScheduler().waitAll();
    }
    else
    {
        for (auto& batch : batches)
        {
            lutBatchTrampoline(&batch);
        }
    }

    // Normalize to display range: raw single-scattering integrals exceed
    // 1.0 (dense atmosphere paths), so scale by the peak channel. Ordering
    // (sun-facing brighter, limb structure) is preserved.
    float peak = 0.0f;
    for (const auto& texel : lut.data)
    {
        for (float c : texel)
        {
            peak = std::max(peak, c);
        }
    }
    if (peak > 0.0f)
    {
        const float inv = 1.0f / peak;
        for (auto& texel : lut.data)
        {
            for (float& c : texel)
            {
                c *= inv;
            }
        }
    }

    return lut;
}

AtmosphereRenderer::AtmosphereRenderer()
{
    params_.planetRadius = 6371000.0f;  // Earth radius in meters
    params_.atmosphereRadius = 6471000.0f;  // Atmosphere extends 100km above surface
    params_.rayleighScatter[0] = 5.8e-6f;   // Red rayleigh coefficient
    params_.rayleighScatter[1] = 13.5e-6f;  // Green rayleigh coefficient
    params_.rayleighScatter[2] = 33.1e-6f;  // Blue rayleigh coefficient
    params_.mieScatter = 2e-5f;             // Mie scattering coefficient
    params_.mieG = 0.76f;                   // Mie phase function asymmetry
    params_.sunDirection[0] = 0.0f;
    params_.sunDirection[1] = 0.0f;
    params_.sunDirection[2] = -1.0f;        // Sun from above
    params_.sunIrradiance = 1361.0f;        // Solar irradiance W/m²
}

bool AtmosphereRenderer::initialize(const AtmosphereParams& params, bool parallel)
{
    params_ = params;
    lut_ = generateAtmosphereLut(params_, parallel);
    initialized_ = true;
    return true;
}

void AtmosphereRenderer::render() const
{
    // GPU shell rendering consumes lut() (see 7.4b); no CPU-side work.
    (void)initialized_;
}

} // namespace starforge::planets
