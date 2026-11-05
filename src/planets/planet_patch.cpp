#include "planets/planet_patch.hpp"

#include <cmath>

namespace starforge::planets
{

std::vector<PlanetPatchVertex> generatePatchGrid(int resolution)
{
    if (resolution < 2) return {};

    std::vector<PlanetPatchVertex> vertices;
    vertices.resize(static_cast<size_t>(resolution) * static_cast<size_t>(resolution));

    for (int y = 0; y < resolution; ++y)
    {
        for (int x = 0; x < resolution; ++x)
        {
            float fx = (static_cast<float>(x) / static_cast<float>(resolution - 1)) * 2.0f - 1.0f;
            float fy = (static_cast<float>(y) / static_cast<float>(resolution - 1)) * 2.0f - 1.0f;
            int idx = y * resolution + x;
            vertices[static_cast<std::size_t>(idx)] = {fx, fy, 0.0f};
        }
    }
    return vertices;
}

std::vector<PlanetPatchVertex> generatePatchGridWithSkirts(int resolution, float skirtDepth)
{
    if (resolution < 2) return {};

    std::vector<PlanetPatchVertex> vertices;
    size_t mainCount = static_cast<size_t>(resolution) * static_cast<size_t>(resolution);
    size_t skirtCount = static_cast<size_t>(resolution) * 4 - 4;
    vertices.resize(mainCount + skirtCount);

    // Generate main grid
    for (int y = 0; y < resolution; ++y)
    {
        for (int x = 0; x < resolution; ++x)
        {
            float fx = (static_cast<float>(x) / static_cast<float>(resolution - 1)) * 2.0f - 1.0f;
            float fy = (static_cast<float>(y) / static_cast<float>(resolution - 1)) * 2.0f - 1.0f;
            int idx = y * resolution + x;
            vertices[static_cast<std::size_t>(idx)] = {fx, fy, 0.0f};
        }
    }

    // Generate skirts - extrude boundary vertices downward (negative Z)
    size_t skirtIdx = mainCount;
    // Bottom edge
    for (int x = 0; x < resolution; ++x)
    {
        float fx = (static_cast<float>(x) / static_cast<float>(resolution - 1)) * 2.0f - 1.0f;
        float fy = -1.0f;
        vertices[skirtIdx++] = {fx, fy, -skirtDepth};
    }
    // Right edge (skip bottom-right corner already added)
    for (int y = 1; y < resolution; ++y)
    {
        float fx = 1.0f;
        float fy = (static_cast<float>(y) / static_cast<float>(resolution - 1)) * 2.0f - 1.0f;
        vertices[skirtIdx++] = {fx, fy, -skirtDepth};
    }
    // Top edge (skip top-right corner already added by right edge)
    for (int x = resolution - 2; x >= 0; --x)
    {
        float fx = (static_cast<float>(x) / static_cast<float>(resolution - 1)) * 2.0f - 1.0f;
        float fy = 1.0f;
        vertices[skirtIdx++] = {fx, fy, -skirtDepth};
    }
    // Left edge (skip top-left and bottom-left corners already added)
    for (int y = resolution - 2; y >= 1; --y)
    {
        float fx = -1.0f;
        float fy = (static_cast<float>(y) / static_cast<float>(resolution - 1)) * 2.0f - 1.0f;
        vertices[skirtIdx++] = {fx, fy, -skirtDepth};
    }

    return vertices;
}

std::vector<std::uint32_t> generatePatchIndices(int resolution, bool withSkirts)
{
    if (resolution < 2) return {};

    std::vector<std::uint32_t> indices;
    int n = resolution;

    // Internal grid triangles: (n-1) x (n-1) quads, 2 triangles each
    size_t triangleCount = static_cast<size_t>(n - 1) * static_cast<size_t>(n - 1) * 6;
    size_t skirtCount = withSkirts ? static_cast<size_t>(n) * 4 - 4 : 0;
    // skirt quads: skirtCount * 2 triangles (6 indices each) - approximate
    indices.reserve(triangleCount + skirtCount * 6);

    // Internal grid
    for (int y = 0; y < n - 1; ++y)
    {
        for (int x = 0; x < n - 1; ++x)
        {
            int i0 = y * n + x;
            int i1 = y * n + (x + 1);
            int i2 = (y + 1) * n + x;
            int i3 = (y + 1) * n + (x + 1);
            indices.push_back(static_cast<std::uint32_t>(i0));
            indices.push_back(static_cast<std::uint32_t>(i2));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(i2));
            indices.push_back(static_cast<std::uint32_t>(i3));
        }
    }

    if (withSkirts)
    {
        int mainCount = n * n;
        int skirtIdx = mainCount;

        // Bottom edge skirts
        for (int x = 0; x < n - 1; ++x)
        {
            int i0 = 0 * n + x;           // grid top-left
            int i1 = 0 * n + (x + 1);     // grid top-right
            int s0 = skirtIdx + x;         // skirt bottom-left
            int s1 = skirtIdx + (x + 1);   // skirt bottom-right
            indices.push_back(static_cast<std::uint32_t>(i0));
            indices.push_back(static_cast<std::uint32_t>(s0));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(s0));
            indices.push_back(static_cast<std::uint32_t>(s1));
        }
        skirtIdx += n;

        // Right edge skirts
        for (int y = 0; y < n - 1; ++y)
        {
            int i0 = y * n + (n - 1);
            int i1 = (y + 1) * n + (n - 1);
            int s0 = skirtIdx + y;
            int s1 = skirtIdx + (y + 1);
            indices.push_back(static_cast<std::uint32_t>(i0));
            indices.push_back(static_cast<std::uint32_t>(s0));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(s0));
            indices.push_back(static_cast<std::uint32_t>(s1));
        }
        skirtIdx += n - 1;

        // Top edge skirts
        for (int x = n - 2; x >= 0; --x)
        {
            int i0 = (n - 1) * n + x;
            int i1 = (n - 1) * n + (x + 1);
            int s0 = skirtIdx + (n - 2 - x);
            int s1 = skirtIdx + (n - 1 - x);
            indices.push_back(static_cast<std::uint32_t>(i0));
            indices.push_back(static_cast<std::uint32_t>(s0));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(s0));
            indices.push_back(static_cast<std::uint32_t>(s1));
        }
        skirtIdx += n - 1;

        // Left edge skirts
        for (int y = n - 2; y >= 1; --y)
        {
            int i0 = (y + 1) * n + 0;
            int i1 = y * n + 0;
            int s0 = skirtIdx + (n - 2 - y);
            int s1 = skirtIdx + (n - 1 - y);
            indices.push_back(static_cast<std::uint32_t>(i0));
            indices.push_back(static_cast<std::uint32_t>(s0));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(i1));
            indices.push_back(static_cast<std::uint32_t>(s0));
            indices.push_back(static_cast<std::uint32_t>(s1));
        }
    }

    return indices;
}

} // namespace starforge::planets
