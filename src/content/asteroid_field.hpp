#pragma once

// Deterministic, world-space asteroid field.
//
// The field is a pure function of world position: rocks live in fixed
// world-space cells and are generated from a hash of the cell coordinate.
// Flying away and back therefore reproduces exactly the same rocks, and
// nothing "pops in" because of streaming decisions - new rocks only appear
// where the field genuinely has them.
//
// This is deliberately separate from the astronomical cell hierarchy in
// cell_traversal.hpp: that one addresses 2^20 m cells so it can address
// interstellar distances, which is far too coarse to populate the space a
// ship actually flies through.

#include <cstdint>
#include <vector>

namespace starforge::content
{

// One rock produced by the procedural field.
struct AsteroidSample
{
    double x = 0.0; // world position, metres
    double y = 0.0;
    double z = 0.0;
    float scale = 1.0f;    // radius, metres
    float yaw = 0.0f;      // orientation, radians
    float pitch = 0.0f;
    float roll = 0.0f;
    float spinAxis[3] = {0.0f, 1.0f, 0.0f}; // tumble axis (unit)
    float spinRate = 0.0f;                     // radians per second
    // Distance band over which the rock dissolves in and out. The renderer
    // fades the rock across this band so the edge of the streamed field
    // never pops.
    float fadeStart = 0.0f; // fully visible at or below this distance
    float fadeEnd = 0.0f;   // fully dissolved at or above this distance
    std::uint32_t seed = 0; // stable per-rock id (never derived from the camera)
};

// Generates the rocks of one population (one cell size, one size range).
class AsteroidField
{
public:
    struct Config
    {
        std::uint64_t worldSeed = 42;
        double cellSize = 256.0; // metres per cell
        // Mean rocks per cell before density modulation. The effective count
        // varies with the local density field, so some cells are empty and
        // others are packed.
        float rocksPerCell = 1.2f;
        float minScale = 8.0f;  // metres
        float maxScale = 40.0f; // metres
        // Dissolve band, in metres from the viewer.
        float fadeStart = 1500.0f;
        float fadeEnd = 2400.0f;
    };

    explicit AsteroidField(Config config);

    // Generate the rocks of one world-space cell. Deterministic for a given
    // seed and cell coordinate; the same cell always yields the same rocks.
    void generateCell(std::int64_t cx, std::int64_t cy, std::int64_t cz,
                      std::vector<AsteroidSample>& out) const;

    // Index of the cell containing a world coordinate.
    [[nodiscard]] std::int64_t cellIndex(double metres) const noexcept;

    // Cell origin (world metres) of a cell index.
    [[nodiscard]] double cellOrigin(std::int64_t index) const noexcept;

    // Squared distance from a point to the nearest corner of a cell. Used to
    // decide whether a cell is close enough to be worth streaming.
    [[nodiscard]] double cellDistanceSquared(double x, double y, double z,
                                            std::int64_t cx, std::int64_t cy,
                                            std::int64_t cz) const noexcept;

    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] double cellSize() const noexcept { return config_.cellSize; }

private:
    Config config_;
};

} // namespace starforge::content
