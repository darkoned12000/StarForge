#pragma once

#include "math/math.hpp"
#include "planets/cubed_sphere.hpp"
#include "planets/planet_patch.hpp"

#include <cstdint>
#include <vector>

namespace starforge::planets
{

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
