#pragma once

#include "starforge/asset.hpp"
#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>

namespace starforge
{

// Typed material capabilities (bitmask). Combine with | in Toon::Params.
enum class ToonCapability : std::uint32_t
{
    None = 0,
    Emissive = 1u,
    Fresnel = 2u,
    Faceted = 4u,
    ScatteringRim = 8u,
    ShadowDetail = 16u,
    Textured = 32u,
};

inline ToonCapability operator|(ToonCapability a, ToonCapability b) noexcept
{
    return static_cast<ToonCapability>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline ToonCapability operator&(ToonCapability a, ToonCapability b) noexcept
{
    return static_cast<ToonCapability>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

// Public toon (cel-shaded) facade: quantized diffuse bands, stepped
// specular, rim light, and an inverted-hull outline pass. Owns its
// pipelines and bundled shader artifacts; callers supply only
// scene-level parameters (view matrices, light direction, colors,
// band count, rim strength, outline thickness in pixels). Move-only;
// the renderer passed to create() must outlive the toon renderer.
// Call Renderer::waitIdle() before destroying or move-assigning a
// toon renderer that has recorded frames.
class Toon
{
public:
    struct CreateInfo
    {
        // Directory containing the bundled toon shaders. Empty loads
        // the bundled artifacts from the build's shader directory.
        std::string shaderDirectory;
    };

    struct Context
    {
        // Clip matrix (projection * modelView) and the modelView matrix
        // (view space: camera at origin looking down -Z).
        math::Mat4f viewProj{};
        math::Mat4f modelView{1.0f};
        // Direction toward the light, in view space.
        math::Vec3f lightDirectionView{0.0f, 0.0f, 1.0f};
        // Target resolution in pixels; keeps outline thickness constant
        // in screen space. Zero falls back to the frame extent.
        math::Vec2f resolution{0.0f, 0.0f};
    };

    struct Params
    {
        math::Vec4f baseColor{1.0f, 0.25f, 0.2f, 1.0f};
        math::Vec4f shadowColor{0.25f, 0.05f, 0.1f, 1.0f};
        // Optional middle color of the diffuse ramp. Alpha == 0 means the
        // midpoint of shadowColor and baseColor.
        math::Vec4f midColor{0.0f, 0.0f, 0.0f, 0.0f};
        float bandCount = 3.0f;
        float rimStrength = 0.5f;
        float outlineWidth = 2.0f; // pixels

        // Composable material capabilities (bitmask, decoded by the shader):
        // 1 = emissive, 2 = fresnel shield, 4 = faceted, 8 = scattering rim,
        // 16 = shadows/attenuation/detail. They are combined on one material,
        // not exposed as separate shaders.
        std::uint32_t capabilities = 0;
        math::Vec4f emissiveColor{0.0f, 0.0f, 0.0f, 0.0f}; // rgb + intensity
        math::Vec4f fresnelColor{0.0f, 0.0f, 0.0f, 0.0f};  // rgb + strength
        math::Vec4f materialParams{0.0f, 0.0f, 0.0f, 0.0f}; // facet, shadow, attenuation, detail
        math::Vec4f highlightColor{0.0f, 0.0f, 0.0f, 0.0f}; // fourth ramp stop + threshold
    };

    // One mesh vertex: position, shading normal, baked smooth outline normal.
    // All values are in object space; no Vulkan types are exposed.
    struct MeshVertex
    {
        math::Vec3f position{};
        math::Vec3f normal{};
        math::Vec3f outlineNormal{};
    };

    // GPU-owned toon mesh. Move-only; the Toon renderer that created it must
    // outlive it. Create from object-space vertices via Toon::createMesh().
    class ToonMesh
    {
    public:
        ToonMesh() = default;
        ~ToonMesh();
        ToonMesh(ToonMesh&&) noexcept;
        ToonMesh& operator=(ToonMesh&&) noexcept;
        ToonMesh(const ToonMesh&) = delete;
        ToonMesh& operator=(const ToonMesh&) = delete;

        [[nodiscard]] std::uint32_t vertexCount() const noexcept;

    private:
        friend class Toon;
        struct Impl;
        explicit ToonMesh(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };

    [[nodiscard]] static std::expected<Toon, std::string> create(Renderer& renderer, const CreateInfo& info);

    ~Toon();
    Toon(Toon&& other) noexcept;
    Toon& operator=(Toon&& other) noexcept;
    Toon(const Toon&) = delete;
    Toon& operator=(const Toon&) = delete;

    // Records the toon mesh (base pass, then outline pass) into an
    // active frame (inside Renderer::renderFrame).
    void record(FrameContext& frameContext, const Context& context, const Params& params);

    // Uploads a mesh into renderer-owned GPU memory. The renderer must
    // outlive every mesh it creates. Empty vertex lists are rejected.
    [[nodiscard]] std::expected<ToonMesh, std::string>
    createMesh(std::span<const MeshVertex> vertices) const;

    // Creates a textured toon mesh from decoded glTF asset data. UVs,
    // tangents and the embedded base-colour texture are uploaded; map the
    // asset's material base-colour factor into Toon::Params yourself and set
    // capability bit 32 to enable texture sampling.
    [[nodiscard]] std::expected<ToonMesh, std::string>
    createMeshFromAsset(const MeshData& mesh) const;

    // M17.5: same as above but uploads a caller-supplied (streamed) mip as
    // the base-colour texture instead of the embedded full-resolution one.
    [[nodiscard]] std::expected<ToonMesh, std::string>
    createMeshFromAsset(const MeshData& mesh, std::span<const std::uint8_t> rgba,
                        std::uint32_t textureWidth, std::uint32_t textureHeight) const;

    // Records a draw with explicit mesh geometry (base + outline, or the
    // additive pass for emissive materials).
    void recordMesh(FrameContext& frameContext, const Context& context,
                    const Params& params, const ToonMesh& mesh);

    // Standalone helper: clears, records the toon mesh, and presents.
    [[nodiscard]] std::expected<Renderer::FrameResult, std::string>
    render(Renderer& renderer, const Context& context, const Params& params);

    // Recreates the pipelines for the renderer's current color format
    // after a swapchain recreation that changed formats.
    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit Toon(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
