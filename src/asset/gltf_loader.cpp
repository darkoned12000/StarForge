#include "asset/gltf_loader.hpp"

#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

float readComponent(const std::uint8_t* p, std::uint32_t componentType, bool normalized)
{
    switch (componentType)
    {
    case 5120: {
        const auto v = static_cast<std::int8_t>(*p);
        return normalized ? std::max(-1.0f, static_cast<float>(v) / 127.0f) : static_cast<float>(v);
    }
    case 5121: {
        const auto v = *p;
        return normalized ? static_cast<float>(v) / 255.0f : static_cast<float>(v);
    }
    case 5122: {
        std::int16_t v;
        std::memcpy(&v, p, sizeof(v));
        return normalized ? std::max(-1.0f, static_cast<float>(v) / 32767.0f) : static_cast<float>(v);
    }
    case 5123: {
        std::uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v);
    }
    case 5125: {
        std::uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return normalized ? static_cast<float>(static_cast<double>(v) / 4294967295.0) : static_cast<float>(v);
    }
    case 5126: {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    default:
        return 0.0f;
    }
}

// Holds every binary buffer and decoded image for one glTF file. GLB supplies
// a single embedded buffer; ASCII .gltf may reference external files/URIs.
struct LoadContext
{
    std::vector<std::vector<std::uint8_t>> buffers;
    std::vector<TextureData> images;

    [[nodiscard]] const std::uint8_t* bufferData(std::size_t index) const noexcept
    {
        return index < buffers.size() ? buffers[index].data() : nullptr;
    }
    [[nodiscard]] std::size_t bufferSize(std::size_t index) const noexcept
    {
        return index < buffers.size() ? buffers[index].size() : 0;
    }
};

int base64Value(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<std::uint8_t> decodeDataUri(const std::string& uri)
{
    const auto comma = uri.find(',');
    if (comma == std::string::npos || uri.rfind("data:", 0) != 0) return {};
    std::vector<std::uint8_t> out;
    std::size_t accumulator = 0;
    int bits = 0;
    for (std::size_t i = comma + 1; i < uri.size(); ++i)
    {
        const int six = base64Value(uri[i]);
        if (six < 0) continue; // '=', whitespace or other metadata
        accumulator = (accumulator << 6) | static_cast<std::size_t>(six);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xFFu));
        }
    }
    return out;
}

std::vector<std::uint8_t> readUriBytes(const std::string& uri, const std::string& baseDir)
{
    if (uri.rfind("data:", 0) == 0) return decodeDataUri(uri);
    std::ifstream file(std::filesystem::path(baseDir) / uri, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return bytes;
}

TextureData decodeImageBytes(const std::uint8_t* data, std::size_t size)
{
    TextureData result;
    if (!data || size == 0) return result;
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels) return result;
    result.width = static_cast<std::uint32_t>(w);
    result.height = static_cast<std::uint32_t>(h);
    result.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return result;
}

void resolveImages(const json& root, const std::string& baseDir, LoadContext& context)
{
    if (!root.contains("images")) return;
    context.images.resize(root["images"].size());
    for (std::size_t i = 0; i < root["images"].size(); ++i)
    {
        const auto& image = root["images"][i];
        if (image.contains("bufferView"))
        {
            const auto viewIndex = image["bufferView"].get<std::size_t>();
            if (viewIndex >= root.at("bufferViews").size()) continue;
            const auto& view = root["bufferViews"][viewIndex];
            const auto bufferIndex = view.value("buffer", std::size_t{0});
            const auto offset = view.value("byteOffset", std::size_t{0});
            const auto length = view.value("byteLength", std::size_t{0});
            if (offset + length <= context.bufferSize(bufferIndex))
                context.images[i] = decodeImageBytes(context.bufferData(bufferIndex) + offset, length);
        }
        else if (image.contains("uri"))
        {
            const auto bytes = readUriBytes(image["uri"].get<std::string>(), baseDir);
            context.images[i] = decodeImageBytes(bytes.data(), bytes.size());
        }
    }
}

// Reads one accessor into a flat float array (count * components).
bool readAccessor(const json& root, int accessorIndex, const LoadContext& context,
                  std::vector<float>& out)
{
    if (accessorIndex < 0) return false;
    const auto& accessors = root.at("accessors");
    if (static_cast<std::size_t>(accessorIndex) >= accessors.size()) return false;
    const auto& accessor = accessors[accessorIndex];
    if (accessor.contains("sparse"))
        return false; // Sparse accessors are not supported; reject instead of decoding wrong data.
    const std::uint32_t componentType = accessor.value("componentType", 0u);
    const std::size_t components = typeCount(accessor.value("type", ""));
    const std::size_t count = accessor.value("count", std::size_t{0});
    const bool normalized = accessor.value("normalized", false);
    if (components == 0 || count == 0 || componentSize(componentType) == 0) return false;

    std::size_t bufferIndex = 0;
    std::size_t base = accessor.value("byteOffset", std::size_t{0});
    std::size_t stride = components * componentSize(componentType);
    if (accessor.contains("bufferView"))
    {
        const auto& views = root.at("bufferViews");
        const std::size_t viewIndex = accessor["bufferView"].get<std::size_t>();
        if (viewIndex >= views.size()) return false;
        const auto& view = views[viewIndex];
        bufferIndex = view.value("buffer", std::size_t{0});
        base += view.value("byteOffset", std::size_t{0});
        if (view.contains("byteStride")) stride = view["byteStride"].get<std::size_t>();
    }
    if (stride < components * componentSize(componentType)) return false;
    const auto* bin = context.bufferData(bufferIndex);
    const auto binSize = context.bufferSize(bufferIndex);
    if (!bin) return false;

    out.assign(count * components, 0.0f);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t offset = base + i * stride;
        if (offset + components * componentSize(componentType) > binSize) return false;
        for (std::size_t c = 0; c < components; ++c)
            out[i * components + c] = readComponent(bin + offset + c * componentSize(componentType),
                                                    componentType, normalized);
    }
    return true;
}

bool readIndexAccessor(const json& root, int accessorIndex, const LoadContext& context,
                       std::vector<std::uint32_t>& out)
{
    if (accessorIndex < 0) return false;
    const auto& accessors = root.at("accessors");
    if (static_cast<std::size_t>(accessorIndex) >= accessors.size()) return false;
    const auto& accessor = accessors[accessorIndex];
    if (accessor.contains("sparse")) return false;
    const std::uint32_t componentType = accessor.value("componentType", 0u);
    if (typeCount(accessor.value("type", "")) != 1) return false;
    const std::size_t count = accessor.value("count", std::size_t{0});
    const std::size_t compSize = componentSize(componentType);
    if (count == 0 || compSize == 0) return false;
    if (componentType != 5121u && componentType != 5123u && componentType != 5125u)
        return false;

    std::size_t bufferIndex = 0;
    std::size_t base = accessor.value("byteOffset", std::size_t{0});
    std::size_t stride = compSize;
    if (accessor.contains("bufferView"))
    {
        const auto& views = root.at("bufferViews");
        const std::size_t viewIndex = accessor["bufferView"].get<std::size_t>();
        if (viewIndex >= views.size()) return false;
        const auto& view = views[viewIndex];
        bufferIndex = view.value("buffer", std::size_t{0});
        base += view.value("byteOffset", std::size_t{0});
        if (view.contains("byteStride")) stride = view["byteStride"].get<std::size_t>();
    }
    if (stride < compSize) return false;
    const auto* bin = context.bufferData(bufferIndex);
    const auto binSize = context.bufferSize(bufferIndex);
    if (!bin) return false;

    out.assign(count, 0u);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t offset = base + i * stride;
        if (offset + compSize > binSize) return false;
        const auto* p = bin + offset;
        switch (componentType)
        {
        case 5121: out[i] = static_cast<std::uint32_t>(*p); break;
        case 5123: {
            std::uint16_t v;
            std::memcpy(&v, p, sizeof(v));
            out[i] = static_cast<std::uint32_t>(v);
            break;
        }
        case 5125: {
            std::uint32_t v;
            std::memcpy(&v, p, sizeof(v));
            out[i] = v;
            break;
        }
        default: return false;
        }
    }
    return true;
}

MaterialData readMaterial(const json& root, int materialIndex, const LoadContext& context)
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
                material.baseColorFactor[i] = factor[i].get<float>();
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
                if (imageIndex >= 0 && static_cast<std::size_t>(imageIndex) < context.images.size())
                {
                    material.baseColorTexture = context.images[imageIndex];
                    material.hasBaseColorTexture = material.baseColorTexture.width > 0;
                }
            }
        }

    if (source.contains("emissiveTexture") && source["emissiveTexture"].contains("index") &&
        root.contains("textures"))
    {
        const std::size_t textureIndex = source["emissiveTexture"]["index"].get<std::size_t>();
        const auto& textures = root["textures"];
        if (textureIndex < textures.size() && textures[textureIndex].contains("source"))
        {
            const int imageIndex = textures[textureIndex]["source"].get<int>();
            if (imageIndex >= 0 && static_cast<std::size_t>(imageIndex) < context.images.size())
            {
                material.emissiveTexture = context.images[imageIndex];
                material.hasEmissiveTexture = material.emissiveTexture.width > 0;
            }
        }
    }
    }
    return material;
}

std::expected<GltfAsset, std::string> decodeMeshes(const json& root, const LoadContext& context)
{
    if (!root.contains("meshes")) return std::unexpected("glTF has no meshes");
    GltfAsset asset;
    for (const auto& sourceMesh : root["meshes"])
    {
        if (!sourceMesh.contains("primitives")) continue;
        for (const auto& primitive : sourceMesh["primitives"])
        {
            if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION")) continue;
            const auto& attributes = primitive["attributes"];

            std::vector<float> positions, normals, uvs, tangents;
            std::vector<std::uint32_t> indices;
            if (!readAccessor(root, attributes["POSITION"].get<int>(), context, positions))
                return std::unexpected("Failed to read POSITION accessor");
            const std::size_t vertexCount = positions.size() / 3;
            if (vertexCount == 0) continue;

            if (attributes.contains("NORMAL"))
                readAccessor(root, attributes["NORMAL"].get<int>(), context, normals);
            if (attributes.contains("TEXCOORD_0"))
                readAccessor(root, attributes["TEXCOORD_0"].get<int>(), context, uvs);
            if (attributes.contains("TANGENT"))
                readAccessor(root, attributes["TANGENT"].get<int>(), context, tangents);
            if (primitive.contains("indices") &&
                !readIndexAccessor(root, primitive["indices"].get<int>(), context, indices))
                return std::unexpected("Failed to read indices accessor");

            MeshData mesh;
            mesh.name = sourceMesh.value("name", "");
            mesh.material = readMaterial(root,
                primitive.contains("material") ? primitive["material"].get<int>() : -1, context);

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
                    for (std::size_t c = 0; c < 4; ++c) vertex.tangent[c] = tangents[i * 4 + c];
                mesh.vertices.push_back(vertex);
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size() - 1));
            };

            if (!indices.empty())
            {
                for (const std::uint32_t index : indices)
                {
                    if (index < vertexCount) emitVertex(static_cast<std::size_t>(index));
                }
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

} // namespace

std::expected<GltfAsset, std::string> loadGlb(const std::uint8_t* data, std::size_t size)
{
    if (!data || size < 20) return std::unexpected("GLB buffer is too small");
    if (readU32(data) != kGlbMagic) return std::unexpected("GLB magic mismatch");
    if (readU32(data + 4) != kGlbVersion) return std::unexpected("Only glTF 2.0 is supported");

    std::size_t offset = 12;
    std::string jsonText;
    LoadContext context;
    while (offset + 8 <= size)
    {
        const std::uint32_t chunkLength = readU32(data + offset);
        const std::uint32_t chunkType = readU32(data + offset + 4);
        offset += 8;
        if (chunkLength > size - offset) return std::unexpected("GLB chunk overruns buffer");
        if (chunkType == kJsonChunk)
            jsonText.assign(reinterpret_cast<const char*>(data + offset), chunkLength);
        else if (chunkType == kBinChunk)
            context.buffers.push_back({data + offset, data + offset + chunkLength});
        offset += chunkLength;
    }
    if (jsonText.empty()) return std::unexpected("GLB has no JSON chunk");
    if (context.buffers.empty()) return std::unexpected("GLB has no binary chunk");

    json root;
    try
    {
        root = json::parse(jsonText);
        resolveImages(root, {}, context);
        return decodeMeshes(root, context);
    }
    catch (const json::exception& e)
    {
        return std::unexpected(std::string("Invalid glTF JSON: ") + e.what());
    }
    catch (const std::exception& e)
    {
        return std::unexpected(std::string("glTF decode failed: ") + e.what());
    }
}

std::expected<GltfAsset, std::string> loadGltf(const std::uint8_t* data, std::size_t size,
                                              const std::string& baseDir)
{
    if (!data || size == 0) return std::unexpected("glTF buffer is empty");
    if (size >= 4 && readU32(data) == kGlbMagic) return loadGlb(data, size);

    json root;
    try
    {
        root = json::parse(reinterpret_cast<const char*>(data),
                           reinterpret_cast<const char*>(data) + size);
    }
    catch (const json::exception& e) { return std::unexpected(std::string("Invalid glTF JSON: ") + e.what()); }

    try
    {
        LoadContext context;
        if (root.contains("buffers"))
            for (const auto& buffer : root["buffers"])
            {
                if (!buffer.contains("uri"))
                    return std::unexpected("External glTF requires buffer URIs");
                auto bytes = readUriBytes(buffer["uri"].get<std::string>(), baseDir);
                if (bytes.empty()) return std::unexpected("Failed to resolve glTF buffer URI");
                context.buffers.push_back(std::move(bytes));
            }
        resolveImages(root, baseDir, context);
        return decodeMeshes(root, context);
    }
    catch (const json::exception& e)
    {
        return std::unexpected(std::string("Invalid glTF JSON: ") + e.what());
    }
    catch (const std::exception& e)
    {
        return std::unexpected(std::string("glTF decode failed: ") + e.what());
    }
}

} // namespace starforge::asset
