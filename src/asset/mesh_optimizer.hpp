#pragma once

// M17.4: meshoptimizer-backed vertex welding, cache/fetch optimisation and
// LOD simplification. All functions operate on the CPU-only MeshData layout.

#include "gltf_loader.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace starforge::asset
{

struct MeshStats
{
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::size_t bytes = 0;
};

[[nodiscard]] MeshStats meshStats(const MeshData& mesh) noexcept;

// Welds duplicate vertices, optimises the vertex cache and fetch order, then
// re-expands indices into the flat per-triangle stream used by the toon BDA
// path. The visible geometry is unchanged; only the layout improves.
[[nodiscard]] MeshStats optimizeMesh(MeshData& mesh);

// Reduces the triangle count to roughly `ratio` (0 < ratio <= 1) of the
// source. Attributes are preserved (no new vertices are synthesised).
[[nodiscard]] std::expected<MeshData, std::string> simplifyMesh(const MeshData& mesh, float ratio);

// Builds a LOD chain from a base mesh: index 0 is the untouched base, the
// remaining levels are simplified by the given ratios (largest first).
[[nodiscard]] std::expected<std::vector<MeshData>, std::string>
generateLodLevels(const MeshData& base, std::span<const float> ratios);

} // namespace starforge::asset
