#pragma once

#include "starforge/math.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace starforge
{

// CPU-side decoded glTF 2.0 asset. Holds positions, normals, UVs, tangents,
// triangle indices and glTF base-colour material data (including embedded
// textures). No Vulkan types are exposed; rendering is done by Toon/Mesh.
struct MeshVertexData
{
    math::Vec3f position{};
    math::Vec3f normal{};
    math::Vec2f uv{};
    math::Vec4f tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

struct MeshMaterialData
{
    math::Vec4f baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    bool hasBaseColorTexture = false;
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    std::vector<std::uint8_t> textureRGBA; // decoded RGBA8 pixels
    std::string name;
};

struct MeshData
{
    std::vector<MeshVertexData> vertices;
    std::vector<std::uint32_t> indices; // already expanded: i-th vertex
    MeshMaterialData material;
    std::string name;
};

class Asset
{
public:
    static std::expected<Asset, std::string> loadFromMemory(std::span<const std::uint8_t> glbBytes);
    static std::expected<Asset, std::string> loadFromFile(const std::string& path);

    ~Asset();
    Asset(Asset&&) noexcept;
    Asset& operator=(Asset&&) noexcept;
    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;

    [[nodiscard]] std::span<const MeshData> meshes() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return meshes().empty(); }

private:
    struct Impl;
    explicit Asset(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
