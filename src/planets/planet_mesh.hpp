#pragma once

#include "math/math.hpp"
#include "planets/cubed_sphere.hpp"

#include <cstdint>
#include <vector>

namespace starforge::planets
{

// Represents a single patch of planetary surface geometry.
struct PlanetPatch
{
    CubeFace face = CubeFace::PositiveX;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t level = 0;

    // Bounding volume stored in double precision for jitter-free culling.
    // `center` is planet-centered (meters); vertices are generated
    // relative to it (RTC) for single-precision GPUs.
    math::Vec3d center{0.0, 0.0, 0.0};
    double boundingRadius = 0.0;

    // UV bounds in cube face coordinates [-1, 1]
    double uMin = -1.0;
    double uMax = 1.0;
    double vMin = -1.0;
    double vMax = 1.0;

    // Material index for this patch
    std::uint32_t materialIndex = 0;

    // Patch data offset in the vertex buffer (for GPU access)
    std::uint32_t vertexOffset = 0;
};

// Generates PlanetPatch children for a quadtree node at a given level.
// (Distinct from the face-based PlanetQuadtree in planet_quadtree.hpp,
// which owns LOD selection; this class turns a node into patch data.)
class PlanetPatchGenerator
{
public:
    explicit PlanetPatchGenerator(double radius, double maxError);

    // Root patch covering a whole cube face at level 0.
    [[nodiscard]] PlanetPatch createRootPatch(CubeFace face) const;

    // Subdivide a patch into its 4 child patches at level + 1.
    // Returns empty when the patch is already at maxLevel_.
    [[nodiscard]] std::vector<PlanetPatch> subdivide(const PlanetPatch& parent) const;

    [[nodiscard]] std::uint32_t getMaxLevel() const noexcept { return maxLevel_; }
    [[nodiscard]] double getRadius() const noexcept { return radius_; }

private:
    [[nodiscard]] PlanetPatch createPatch(CubeFace face, std::int32_t x, std::int32_t y, std::uint32_t level) const;

    double radius_;
    double maxError_;
    std::uint32_t maxLevel_;
};

// CPU-side mesh for one quadtree patch.
// Positions are relative-to-center (RTC): planet-centered double position
// minus the patch's `center` (double), stored as single-precision offsets
// so planetary-scale coordinates don't jitter on the GPU. Add
// `patch.center` (as float) in the vertex shader / BDA upload to recover
// world position. Normals are unit-length outward directions; UVs are the
// face-local (u, v) coordinates.
struct PlanetPatchMesh
{
    std::vector<math::Vec3f> positions; // RTC offsets from patch.center
    std::vector<math::Vec3f> normals;
    std::vector<math::Vec2f> uvs;
    std::vector<std::uint32_t> indices;
};

// Generate a grid mesh over `patch`'s face-local bounds with
// `subdivisions` quads per side, projected onto a sphere of `radius`.
// Returns an empty mesh when subdivisions == 0.
//
// Known limitation: neighboring patches at different LOD levels share
// edges without skirts/geomorphing (T-junctions). Addressed in 7.2c.
[[nodiscard]] PlanetPatchMesh generatePatchMesh(
    const PlanetPatch& patch, std::uint32_t subdivisions, double radius);

} // namespace starforge::planets
