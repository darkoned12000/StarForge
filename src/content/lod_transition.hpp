#pragma once

#include <vector>

namespace starforge::content
{

// Toon LOD transition state for one object or patch.
//
// The level changes only inside a hysteresis band around a distance
// threshold; inside the band both levels are blended (dithered cross-
// fade by the renderer) and the incoming level's outline pass must be
// suppressed. Outside the band the state is stable, so jittering at a
// threshold cannot oscillate the discrete level.
struct LodTransition
{
    // Level being drawn in full outside the band.
    int level = 0;
    // Cross-fade participants during a transition.
    int fromLevel = 0;
    int toLevel = 0;
    // Weight of toLevel (0..1); 0 outside a band.
    double amount = 0.0;
    bool fading = false;
    // Inverted-hull outlines must skip the incoming (fading) mesh.
    bool incomingOutlineEnabled = true;

    // Mesh owning the contour line. Outline follows the dominant
    // level so the silhouette doesn't stay glued to a ghost mesh until
    // the fade ends.
    [[nodiscard]] int outlineLevel() const noexcept { return (!fading || amount < 0.5) ? fromLevel : toLevel; }
};

class ToonLodHysteresis
{
public:
    // thresholds[i] is the distance at which the selector leaves level i
    // for level i+1. bandMetres is the transition band width around each
    // threshold (must be >= 0; 0 = immediate switch).
    explicit ToonLodHysteresis(std::vector<double> thresholds, double bandMetres = 0.0);

    [[nodiscard]] LodTransition update(double distance);

    [[nodiscard]] int level() const noexcept { return level_; }
    [[nodiscard]] int levelCount() const noexcept { return static_cast<int>(thresholds_.size()) + 1; }

private:
    [[nodiscard]] int desiredLevel(double distance) const noexcept;
    [[nodiscard]] bool withinBand(double distance, double threshold, double& amount) const noexcept;

    std::vector<double> thresholds_;
    double bandMetres_;
    int level_ = 0;
    bool transitioning_ = false;
    int fromLevel_ = 0;
    int toLevel_ = 0;
    double lastDistance_ = 0.0;
    bool hasLastDistance_ = false;
};

} // namespace starforge::content
