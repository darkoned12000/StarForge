#include "content/lod_transition.hpp"

#include <algorithm>
#include <cmath>

namespace starforge::content
{

ToonLodHysteresis::ToonLodHysteresis(std::vector<double> thresholds, double bandMetres)
    : bandMetres_(bandMetres >= 0.0 && std::isfinite(bandMetres) ? bandMetres : 0.0)
{
    thresholds_.reserve(thresholds.size());
    for (const double threshold : thresholds)
    {
        if (threshold > 0.0 && std::isfinite(threshold) &&
            (thresholds_.empty() || threshold > thresholds_.back()))
        {
            thresholds_.push_back(threshold);
        }
    }
    // Adjacent bands must not overlap; otherwise a camera can be inside
    // two transitions at once and the state machine fights between
    // them. Clamp the band to the tightest threshold spacing.
    if (bandMetres_ > 0.0 && thresholds_.size() > 1)
    {
        double minGap = thresholds_.back();
        for (std::size_t i = 1; i < thresholds_.size(); ++i)
        {
            minGap = std::min(minGap, thresholds_[i] - thresholds_[i - 1]);
        }
        bandMetres_ = std::min(bandMetres_, minGap);
    }
}

int ToonLodHysteresis::desiredLevel(double distance) const noexcept
{
    int level = 0;
    for (const double threshold : thresholds_)
    {
        if (distance >= threshold)
        {
            ++level;
        }
    }
    return level;
}

bool ToonLodHysteresis::withinBand(double distance, double threshold, double& amount) const noexcept
{
    if (bandMetres_ <= 0.0)
    {
        amount = 0.0;
        return false;
    }
    const double start = threshold - bandMetres_ * 0.5;
    const double end = threshold + bandMetres_ * 0.5;
    if (distance < start || distance > end)
    {
        amount = 0.0;
        return false;
    }
    amount = (distance - start) / bandMetres_;
    amount = std::clamp(amount, 0.0, 1.0);
    return true;
}

LodTransition ToonLodHysteresis::update(double distance)
{
    LodTransition state;
    if (!(distance >= 0.0) || !std::isfinite(distance))
    {
        state.level = level_;
        state.fromLevel = level_;
        state.toLevel = level_;
        return state;
    }

    const int target = desiredLevel(distance);

    if (!transitioning_)
    {
        // Start a transition whenever the camera is inside any band.
        // Whether approaching from near or far, the side of the
        // threshold decides the destination: a camera on the fine side
        // starts fading to the coarse level, and vice versa. This also
        // covers spawning inside a band, where there is no prior frame
        // to establish the approach direction.
        for (std::size_t i = 0; i < thresholds_.size(); ++i)
        {
            double amount = 0.0;
            if (!withinBand(distance, thresholds_[i], amount))
            {
                continue;
            }
            int destination = -1;
            if (level_ == static_cast<int>(i))
            {
                destination = static_cast<int>(i) + 1;
            }
            else if (level_ == static_cast<int>(i) + 1)
            {
                destination = static_cast<int>(i);
            }
            if (destination < 0 || destination == level_)
            {
                continue;
            }
            transitioning_ = true;
            fromLevel_ = level_;
            toLevel_ = destination;
            state.fading = true;
            state.fromLevel = fromLevel_;
            state.toLevel = toLevel_;
            state.amount = toLevel_ > fromLevel_ ? amount : 1.0 - amount;
            state.level = toLevel_;
            state.incomingOutlineEnabled = false;
            return state;
        }

        if (target == level_)
        {
            state.level = level_;
            state.fromLevel = level_;
            state.toLevel = level_;
            return state;
        }
        level_ = target;
    }
    else
    {
        // Continue an active transition.
        const double boundary =
            toLevel_ > fromLevel_ ? thresholds_[static_cast<std::size_t>(fromLevel_)]
                                  : thresholds_[static_cast<std::size_t>(toLevel_)];
        const double start = boundary - bandMetres_ * 0.5;
        const double end = boundary + bandMetres_ * 0.5;
        const bool headedCoarser = toLevel_ > fromLevel_;

        if ((headedCoarser && distance < start) || (!headedCoarser && distance > end))
        {
            // Reversed: the camera left the band on the source side.
            level_ = fromLevel_;
            transitioning_ = false;
            state.level = level_;
            state.fromLevel = level_;
            state.toLevel = level_;
            return state;
        }
        if ((headedCoarser && distance > end) || (!headedCoarser && distance < start))
        {
            level_ = toLevel_;
            transitioning_ = false;
            state.level = level_;
            state.fromLevel = level_;
            state.toLevel = level_;
            return state;
        }

        double amount = 0.0;
        (void)withinBand(distance, boundary, amount);
        state.fading = true;
        state.fromLevel = fromLevel_;
        state.toLevel = toLevel_;
        state.amount = toLevel_ > fromLevel_ ? amount : 1.0 - amount;
        state.level = toLevel_;
        state.incomingOutlineEnabled = false;
        return state;
    }

    state.level = level_;
    state.fromLevel = level_;
    state.toLevel = level_;
    return state;
}

} // namespace starforge::content
