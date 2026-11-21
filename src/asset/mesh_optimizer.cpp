#include "mesh_optimizer.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace starforge::asset
{

namespace
{
constexpr int quantize(float value, float scale) noexcept
{
    return static_cast<int>(std::lround(value * scale));
}

struct VertexKey
{
    std::array<int, 16> values{};
    bool operator==(const VertexKey&) const noexcept = default;
};

struct VertexKeyHash
{
    std::size_t operator()(const VertexKey& key) const noexcept
    {
        std::size_t hash = 0xcbf29ce484222325ULL;
        for (const int value : key.values)
        {
            hash ^= static_cast<std::uint32_t>(value);
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }
};

VertexKey keyFor(const MeshVertexData& vertex)
{
    VertexKey key;
    key.values[0] = quantize(vertex.position[0], 10000.0f);
    key.values[1] = quantize(vertex.position[1], 10000.0f);
    key.values[2] = quantize(vertex.position[2], 10000.0f);
    key.values[3] = quantize(vertex.normal[0], 1000.0f);
    key.values[4] = quantize(vertex.normal[1], 1000.0f);
    key.values[5] = quantize(vertex.normal[2], 1000.0f);
    key.values[6] = quantize(vertex.uv[0], 10000.0f);
    key.values[7] = quantize(vertex.uv[1], 10000.0f);
    for (std::size_t i = 0; i < 4; ++i)
        key.values[8 + i] = quantize(vertex.tangent[i], 1000.0f);
    return key;
}

// Deduplicates vertices and returns a compact index buffer referencing them.
void weld(const MeshData& source, std::vector<MeshVertexData>& vertices,
          std::vector<unsigned int>& indices)
{
    std::unordered_map<VertexKey, unsigned int, VertexKeyHash> lookup;
    lookup.reserve(source.vertices.size());
    vertices.clear();
    indices.clear();
    indices.reserve(source.indices.size());
    for (const auto index : source.indices)
    {
        if (index >= source.vertices.size())
        {
            vertices.clear();
            indices.clear();
            return;
        }
        const auto& vertex = source.vertices[index];
        const auto key = keyFor(vertex);
        const auto found = lookup.find(key);
        if (found != lookup.end())
        {
            indices.push_back(found->second);
            continue;
        }
        const auto id = static_cast<unsigned int>(vertices.size());
        lookup.emplace(key, id);
        vertices.push_back(vertex);
        indices.push_back(id);
    }
}

// Expands an index buffer back into the flat per-triangle vertex stream.
void expand(const MeshData& source, const std::vector<MeshVertexData>& vertices,
            const std::vector<unsigned int>& indices, MeshData& output)
{
    output = source;
    output.vertices.clear();
    output.indices.clear();
    output.vertices.reserve(indices.size());
    output.indices.reserve(indices.size());
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        const auto index = indices[i];
        if (index >= vertices.size())
        {
            output.vertices.clear();
            output.indices.clear();
            return;
        }
        output.vertices.push_back(vertices[index]);
        output.indices.push_back(static_cast<std::uint32_t>(i));
    }
}

std::vector<float> positions(const std::vector<MeshVertexData>& vertices)
{
    std::vector<float> out(vertices.size() * 3);
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        out[i * 3 + 0] = vertices[i].position[0];
        out[i * 3 + 1] = vertices[i].position[1];
        out[i * 3 + 2] = vertices[i].position[2];
    }
    return out;
}
} // namespace

MeshStats meshStats(const MeshData& mesh) noexcept
{
    return {mesh.vertices.size(), mesh.indices.size() / 3,
            mesh.vertices.size() * sizeof(MeshVertexData) + mesh.indices.size() * sizeof(std::uint32_t)};
}

MeshStats optimizeMesh(MeshData& mesh)
{
    std::vector<MeshVertexData> vertices;
    std::vector<unsigned int> indices;
    weld(mesh, vertices, indices);
    if (vertices.empty() || indices.empty()) return meshStats(mesh);

    std::vector<unsigned int> cacheOptimized(indices.size());
    meshopt_optimizeVertexCache(cacheOptimized.data(), indices.data(), indices.size(), vertices.size());

    std::vector<unsigned int> remap(vertices.size());
    const std::size_t newCount = meshopt_optimizeVertexFetchRemap(
        remap.data(), cacheOptimized.data(), cacheOptimized.size(), vertices.size());

    std::vector<MeshVertexData> remappedVertices(newCount);
    meshopt_remapVertexBuffer(remappedVertices.data(), vertices.data(), vertices.size(),
                              sizeof(MeshVertexData), remap.data());
    std::vector<unsigned int> remappedIndices(cacheOptimized.size());
    meshopt_remapIndexBuffer(remappedIndices.data(), cacheOptimized.data(),
                             cacheOptimized.size(), remap.data());

    expand(mesh, remappedVertices, remappedIndices, mesh);
    return meshStats(mesh);
}

std::expected<MeshData, std::string> simplifyMesh(const MeshData& mesh, float ratio)
{
    if (!(ratio > 0.0f) || ratio > 1.0f)
        return std::unexpected("LOD ratio must be in (0, 1]");
    std::vector<MeshVertexData> vertices;
    std::vector<unsigned int> indices;
    weld(mesh, vertices, indices);
    if (indices.size() < 3) return mesh;

    const std::size_t originalTriangles = indices.size() / 3;
    std::size_t targetTriangles = static_cast<std::size_t>(
        std::floor(static_cast<float>(originalTriangles) * ratio));
    targetTriangles = std::max<std::size_t>(4, std::min(targetTriangles, originalTriangles));
    const std::size_t targetIndices = targetTriangles * 3;

    const auto positionsBuffer = positions(vertices);
    std::vector<unsigned int> simplified(indices.size());
    const std::size_t count = meshopt_simplify(
        simplified.data(), indices.data(), indices.size(), positionsBuffer.data(),
        vertices.size(), sizeof(float) * 3, targetIndices, 0.01f, 0, nullptr);
    if (count < 3 || count > indices.size())
        return std::unexpected("mesh simplification produced no usable geometry");

    simplified.resize(count - (count % 3));
    std::vector<unsigned int> cacheOptimized(simplified.size());
    meshopt_optimizeVertexCache(cacheOptimized.data(), simplified.data(),
                                simplified.size(), vertices.size());

    MeshData result;
    expand(mesh, vertices, cacheOptimized, result);
    return result;
}

std::expected<std::vector<MeshData>, std::string>
generateLodLevels(const MeshData& base, std::span<const float> ratios)
{
    std::vector<MeshData> levels;
    levels.reserve(ratios.size() + 1);
    levels.push_back(base);
    optimizeMesh(levels.front());
    for (const float ratio : ratios)
    {
        auto simplified = simplifyMesh(base, ratio);
        if (!simplified) return std::unexpected(simplified.error());
        optimizeMesh(*simplified);
        levels.push_back(std::move(*simplified));
    }
    return levels;
}

} // namespace starforge::asset
