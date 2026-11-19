#include "asset/gltf_loader.hpp"

#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace starforge::asset
{

using json = nlohmann::json;

namespace
{

constexpr std::uint32_t kGlbMagic = 0x46546C67u; // "glTF"
constexpr std::uint32_t kGlbVersion = 2;
constexpr std::uint32_t kJsonChunk = 0x4E4F534Au;
constexpr std::uint32_t kBinChunk = 0x004E4942u;

std::uint32_t readU32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::size_t componentSize(std::uint32_t componentType)
{
    switch (componentType)
    {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default: return 0;
    }
}

std::size_t typeCount(const std::string& type)
{
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    return 0;
}

float readComponent(const std::uint8_t* p, std::uint32_t componentType)
{
    switch (componentType)
    {
    case 5120: return static_cast<float>(static_cast<std::int8_t>(*p));
    case 5121: return static_cast<float>(*p);
    case 5122: {
        std::int16_t v;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v);
    }
    case 5123: {
        std::uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v);
    }
    case 5125: {
        std::uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v);
    }
    case 5126: {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    default: return 0.0f;
    }
}

// Reads one accessor into a flat float array (count * components).
bool readAccessor(const json& root, int accessorIndex, const std::uint8_t* bin,
                  std::size_t binSize, std::vector<float>& out)
{
    if (accessorIndex < 0) return false;
    const auto& accessors = root.at("accessors");
    if (static_cast<std::size_t>(accessorIndex) >= accessors.size()) return false;
    const auto& accessor = accessors[accessorIndex];
    const std::uint32_t componentType = accessor.value("componentType", 0u);
    const std::size_t components = typeCount(accessor.value("type", ""));
    const std::size_t count = accessor.value("count", std::size_t{0});
    if (components == 0 || count == 0 || componentSize(componentType) == 0) return false;

    std::size_t base = accessor.value("byteOffset", std::size_t{0});
    std::size_t stride = components * componentSize(componentType);
    if (accessor.contains("bufferView"))
    {
        const auto& views = root.at("bufferViews");
        const std::size_t viewIndex = accessor["bufferView"].get<std::size_t>();
        if (viewIndex >= views.size()) return false;
        const auto& view = views[viewIndex];
        base += view.value("byteOffset", std::size_t{0});
        if (view.contains("byteStride")) stride = view["byteStride"].get<std::size_t>();
    }
    if (stride < components * componentSize(componentType)) return false;

    out.assign(count * components, 0.0f);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t offset = base + i * stride;
        if (offset + components * componentSize(componentType) > binSize) return false;
        for (std::size_t c = 0; c < components; ++c)
        {
            out[i * components + c] = readComponent(bin + offset + c * componentSize(componentType), componentType);
        }
    }
    return true;
}

TextureData decodeEmbeddedTexture(const json& root, int imageIndex, const std::uint8_t* bin,
                                  std::size_t binSize)
{
    TextureData result;
    if (imageIndex < 0) return result;
    const auto& images = root.at("images");
    if (static_cast<std::size_t>(imageIndex) >= images.size()) return result;
    const auto& image = images[imageIndex];
    if (!image.contains("bufferView")) return result;
    const auto& views = root.at("bufferViews");
    const std::size_t viewIndex = image["bufferView"].get<std::size_t>();
    if (viewIndex >= views.size()) return result;
    const auto& view = views[viewIndex];
    const std::size_t offset = view.value("byteOffset", std::size_t{0});
    const std::size_t length = view.value("byteLength", std::size_t{0});
    if (offset + length > binSize) return result;
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bin + offset, static_cast<int>(length), &w, &h, &channels, 4);
    if (!pixels) return result;
    result.width = static_cast<std::uint32_t>(w);
    result.height = static_cast<std::uint32_t>(h);
    result.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return result;
}

MaterialData readMaterial(const json& root, int materialIndex, const std::uint8_t* bin,
                          std::size_t binSize)
{
    MaterialData material;
    if (materialIndex < 0) return material;
    const auto& materials = root.at("materials");
    if (static_cast<std::size_t>(materialIndex) >= materials.size()) return material;
    const auto& source = materials[materialIndex];
    material.name = source.value("name", "");
    material.doubleSided = source.value("doubleSided", false);
    material.alphaMode = source.value("alphaMode", std::string("OPAQUE"));
    if (source.contains("emissiveFactor") && source["emissiveFactor"].is_array())
    {
        const auto& factor = source["emissiveFactor"];
        for (std::size_t i = 0; i < std::min<std::size_t>(3, factor.size()); ++i)
            material.emissiveFactor[i] = factor[i].get<float>();
    }
    const auto hasTextureSlot = [&](const char* slot) {
        return source.contains(slot) && source[slot].contains("index") && root.contains("textures");
    };
    material.hasNormalTexture = hasTextureSlot("normalTexture");
    material.hasOcclusionTexture = hasTextureSlot("occlusionTexture");
    material.hasEmissiveTexture = hasTextureSlot("emissiveTexture");
    if (source.contains("pbrMetallicRoughness"))
    {
        const auto& pbr = source["pbrMetallicRoughness"];
        if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array())
        {
            const auto& factor = pbr["baseColorFactor"];
            for (std::size_t i = 0; i < std::min<std::size_t>(4, factor.size()); ++i)
            {
                material.baseColorFactor[i] = factor[i].get<float>();
            }
        }
        material.metallicFactor = pbr.value("metallicFactor", 0.0f);
        material.roughnessFactor = pbr.value("roughnessFactor", 1.0f);
        material.hasMetallicRoughnessTexture =
            pbr.contains("metallicRoughnessTexture") && pbr["metallicRoughnessTexture"].contains("index") &&
            root.contains("textures");
        if (pbr.contains("baseColorTexture") && pbr["baseColorTexture"].contains("index") &&
            root.contains("textures"))
        {
            const std::size_t textureIndex = pbr["baseColorTexture"]["index"].get<std::size_t>();
            const auto& textures = root["textures"];
            if (textureIndex < textures.size() && textures[textureIndex].contains("source"))
            {
                const int imageIndex = textures[textureIndex]["source"].get<int>();
                material.baseColorTexture = decodeEmbeddedTexture(root, imageIndex, bin, binSize);
                material.hasBaseColorTexture = material.baseColorTexture.width > 0;
            }
        }
    }
    return material;
}

} // namespace

std::expected<GltfAsset, std::string> loadGlb(const std::uint8_t* data, std::size_t size)
{
    if (!data || size < 20) return std::unexpected("GLB buffer is too small");
    if (readU32(data) != kGlbMagic) return std::unexpected("GLB magic mismatch");
    if (readU32(data + 4) != kGlbVersion) return std::unexpected("Only glTF 2.0 is supported");

    std::size_t offset = 12;
    std::string jsonText;
    const std::uint8_t* binary = nullptr;
    std::size_t binarySize = 0;
    while (offset + 8 <= size)
    {
        const std::uint32_t chunkLength = readU32(data + offset);
        const std::uint32_t chunkType = readU32(data + offset + 4);
        offset += 8;
        if (chunkLength > size - offset) return std::unexpected("GLB chunk overruns buffer");
        if (chunkType == kJsonChunk)
        {
            jsonText.assign(reinterpret_cast<const char*>(data + offset), chunkLength);
        }
        else if (chunkType == kBinChunk)
        {
            binary = data + offset;
            binarySize = chunkLength;
        }
        offset += chunkLength;
    }
    if (jsonText.empty()) return std::unexpected("GLB has no JSON chunk");
    if (!binary) return std::unexpected("GLB has no binary chunk");

    json root;
    try
    {
        root = json::parse(jsonText);
    }
    catch (const json::exception& e)
    {
        return std::unexpected(std::string("Invalid glTF JSON: ") + e.what());
    }
    if (!root.contains("meshes")) return std::unexpected("glTF has no meshes");

    GltfAsset asset;
    for (const auto& sourceMesh : root["meshes"])
    {
        if (!sourceMesh.contains("primitives")) continue;
        for (const auto& primitive : sourceMesh["primitives"])
        {
            if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION")) continue;
            const auto& attributes = primitive["attributes"];

            std::vector<float> positions, normals, uvs, tangents, indices;
            if (!readAccessor(root, attributes["POSITION"].get<int>(), binary, binarySize, positions))
                return std::unexpected("Failed to read POSITION accessor");
            const std::size_t vertexCount = positions.size() / 3;
            if (vertexCount == 0) continue;

            if (attributes.contains("NORMAL"))
                readAccessor(root, attributes["NORMAL"].get<int>(), binary, binarySize, normals);
            if (attributes.contains("TEXCOORD_0"))
                readAccessor(root, attributes["TEXCOORD_0"].get<int>(), binary, binarySize, uvs);
            if (attributes.contains("TANGENT"))
                readAccessor(root, attributes["TANGENT"].get<int>(), binary, binarySize, tangents);
            if (primitive.contains("indices"))
                readAccessor(root, primitive["indices"].get<int>(), binary, binarySize, indices);

            MeshData mesh;
            mesh.name = sourceMesh.value("name", "");
            mesh.material = readMaterial(root,
                primitive.contains("material") ? primitive["material"].get<int>() : -1, binary, binarySize);

            // Expand indices so the toon BDA path (SV_VertexID, no index
            // buffer) consumes a flat vertex stream, while keeping UV/tangent
            // data for textured materials.
            auto emitVertex = [&](std::size_t i) {
                MeshVertexData vertex;
                vertex.position[0] = positions[i * 3 + 0];
                vertex.position[1] = positions[i * 3 + 1];
                vertex.position[2] = positions[i * 3 + 2];
                if (normals.size() >= (i + 1) * 3)
                {
                    vertex.normal[0] = normals[i * 3 + 0];
                    vertex.normal[1] = normals[i * 3 + 1];
                    vertex.normal[2] = normals[i * 3 + 2];
                }
                if (uvs.size() >= (i + 1) * 2)
                {
                    vertex.uv[0] = uvs[i * 2 + 0];
                    vertex.uv[1] = uvs[i * 2 + 1];
                }
                if (tangents.size() >= (i + 1) * 4)
                {
                    for (std::size_t c = 0; c < 4; ++c) vertex.tangent[c] = tangents[i * 4 + c];
                }
                mesh.vertices.push_back(vertex);
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size() - 1));
            };

            if (!indices.empty())
            {
                for (float index : indices) emitVertex(static_cast<std::size_t>(index));
            }
            else
            {
                for (std::size_t i = 0; i < vertexCount; ++i) emitVertex(i);
            }
            asset.meshes.push_back(std::move(mesh));
        }
    }
    if (asset.meshes.empty()) return std::unexpected("glTF contained no drawable primitives");
    return asset;
}

} // namespace starforge::asset
