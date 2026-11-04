#include "stats/statistics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace starforge::stats
{

namespace
{

// Filter out non-finite values.
std::vector<double> filterFinite(std::span<const double> samples)
{
    std::vector<double> result;
    result.reserve(samples.size());
    for (double v : samples)
    {
        if (std::isfinite(v))
        {
            result.push_back(v);
        }
    }
    return result;
}

// Filter and convert float samples in a single pass.
std::vector<double> filterFiniteFloat(std::span<const float> samples)
{
    std::vector<double> result;
    result.reserve(samples.size());
    for (float v : samples)
    {
        double dv = static_cast<double>(v);
        if (std::isfinite(dv))
        {
            result.push_back(dv);
        }
    }
    return result;
}

} // namespace

double percentile(std::span<const double> sortedData, double p)
{
    if (sortedData.empty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    if (std::isnan(p))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Clamp p to [0, 100].
    p = std::clamp(p, 0.0, 100.0);

    const std::size_t n = sortedData.size();
    if (n == 1)
    {
        return sortedData[0];
    }

    // Linear interpolation (NumPy default, R type 7).
    const double index = (p / 100.0) * static_cast<double>(n - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(index));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(index));

    if (lo == hi)
    {
        return sortedData[lo];
    }

    const double frac = index - static_cast<double>(lo);
    return sortedData[lo] + frac * (sortedData[hi] - sortedData[lo]);
}

Statistics compute(std::span<const double> samples)
{
    Statistics stats;
    stats.count = 0;
    stats.valid = false;

    auto finite = filterFinite(samples);
    if (finite.empty())
    {
        return stats;
    }

    std::sort(finite.begin(), finite.end());

    stats.count = finite.size();
    stats.valid = true;
    stats.min = finite.front();
    stats.max = finite.back();

    // Mean.
    double sum = 0.0;
    for (double v : finite)
    {
        sum += v;
    }
    stats.mean = sum / static_cast<double>(finite.size());

    // Sample standard deviation (n-1 denominator).
    if (finite.size() >= 2)
    {
        double sqSum = 0.0;
        for (double v : finite)
        {
            double diff = v - stats.mean;
            sqSum += diff * diff;
        }
        stats.stddev = std::sqrt(sqSum / static_cast<double>(finite.size() - 1));
    }
    else
    {
        stats.stddev = 0.0;
    }

    // Percentiles.
    stats.median = percentile(finite, 50.0);
    stats.p95 = percentile(finite, 95.0);
    stats.p99 = percentile(finite, 99.0);

    return stats;
}

Statistics compute(std::span<const float> samples)
{
    auto finite = filterFiniteFloat(samples);
    std::sort(finite.begin(), finite.end());

    Statistics stats;
    stats.count = 0;
    stats.valid = false;

    if (finite.empty())
    {
        return stats;
    }

    stats.count = finite.size();
    stats.valid = true;
    stats.min = finite.front();
    stats.max = finite.back();

    double sum = 0.0;
    for (double v : finite)
    {
        sum += v;
    }
    stats.mean = sum / static_cast<double>(finite.size());

    if (finite.size() >= 2)
    {
        double sqSum = 0.0;
        for (double v : finite)
        {
            double diff = v - stats.mean;
            sqSum += diff * diff;
        }
        stats.stddev = std::sqrt(sqSum / static_cast<double>(finite.size() - 1));
    }
    else
    {
        stats.stddev = 0.0;
    }

    stats.median = percentile(finite, 50.0);
    stats.p95 = percentile(finite, 95.0);
    stats.p99 = percentile(finite, 99.0);

    return stats;
}

} // namespace starforge::stats
