#include "asteroid_field.hpp"

#include "vulkan/vulkan_pipeline.hpp"
#include "internal/effect_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <type_traits>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

std::uint32_t hash32(std::uint32_t x)
{
    x = x * 1664525u + 1013904223u;
    x ^= x >> 16u;
    return x;
}

// Deterministic 0..1 value from a 32-bit hash.
float hash01(std::uint32_t x)
{
    return static_cast<float>(hash32(x)) / 4294967296.0f;
}

// 3D value noise on an integer lattice with a smoothstep interpolant.
// Cheap, allocation-free and deterministic, so the same noise field can be
// sampled on the CPU (mesh displacement) and mirrored in the shader.
float valueNoise3(float x, float y, float z, std::uint32_t seed)
{
    const float xi = std::floor(x);
    const float yi = std::floor(y);
    const float zi = std::floor(z);
    const auto ix = static_cast<std::int64_t>(xi);
    const auto iy = static_cast<std::int64_t>(yi);
    const auto iz = static_cast<std::int64_t>(zi);
    const float tx = x - xi;
    const float ty = y - yi;
    const float tz = z - zi;
    // Smoothstep (quintic) keeps the second derivative continuous, which
    // matters: plain linear interpolation shows creases in the displacement.
    const float ux = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
    const float uy = ty * ty * ty * (ty * (ty * 6.0f - 15.0f) + 10.0f);
    const float uz = tz * tz * tz * (tz * (tz * 6.0f - 15.0f) + 10.0f);

    const auto corner = [&](std::int64_t cx, std::int64_t cy, std::int64_t cz) {
        std::uint32_t h = seed;
        h = hash32(h ^ static_cast<std::uint32_t>(cx) * 0x9e3779b9u);
        h = hash32(h ^ static_cast<std::uint32_t>(cy) * 0x85ebca6bu);
        h = hash32(h ^ static_cast<std::uint32_t>(cz) * 0xc2b2ae35u);
        return hash01(h);
    };

    const float c000 = corner(ix, iy, iz);
    const float c100 = corner(ix + 1, iy, iz);
    const float c010 = corner(ix, iy + 1, iz);
    const float c110 = corner(ix + 1, iy + 1, iz);
    const float c001 = corner(ix, iy, iz + 1);
    const float c101 = corner(ix + 1, iy, iz + 1);
    const float c011 = corner(ix, iy + 1, iz + 1);
    const float c111 = corner(ix + 1, iy + 1, iz + 1);

    const float x00 = c000 + (c100 - c000) * ux;
    const float x10 = c010 + (c110 - c010) * ux;
    const float x01 = c001 + (c101 - c001) * ux;
    const float x11 = c011 + (c111 - c011) * ux;
    const float y0 = x00 + (x10 - x00) * uy;
    const float y1 = x01 + (x11 - x01) * uy;
    return y0 + (y1 - y0) * uz;
}

// Fractal sum of value noise, returned in [-1, 1].
float fbm3(float x, float y, float z, std::uint32_t seed, int octaves)
{
    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += (valueNoise3(x * freq, y * freq, z * freq, seed + static_cast<std::uint32_t>(i) * 7919u) * 2.0f - 1.0f) *
               amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        freq *= 2.03f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

// A crater: a spherical cap dented into the surface, with a raised rim.
// `dir` is the unit direction of the crater centre, `radius` its angular
// radius in radians and `depth` how far it digs below the surface.
struct Crater
{
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
    float radius = 0.0f;
    float depth = 0.0f;
};

// Recompute per-vertex normals from the triangle mesh (area-weighted
// face-normal accumulation). The vertex layout is interleaved
// [pos.xyz, pos.w, normal.xyz, normal.w] with a 32-byte stride.
void recomputeNormals(std::vector<float>& vertices, const std::vector<std::uint32_t>& indices,
                      std::size_t rows, std::size_t cols)
{
    const std::size_t vertexCount = vertices.size() / 8;
    std::vector<float> accum(vertexCount * 3, 0.0f);

    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const auto ia = static_cast<std::size_t>(indices[i]) * 8;
        const auto ib = static_cast<std::size_t>(indices[i + 1]) * 8;
        const auto ic = static_cast<std::size_t>(indices[i + 2]) * 8;

        const float ax = vertices[ia];
        const float ay = vertices[ia + 1];
        const float az = vertices[ia + 2];
        const float e1x = vertices[ib] - ax;
        const float e1y = vertices[ib + 1] - ay;
        const float e1z = vertices[ib + 2] - az;
        const float e2x = vertices[ic] - ax;
        const float e2y = vertices[ic + 1] - ay;
        const float e2z = vertices[ic + 2] - az;

        // Un-normalised cross product: its length is twice the triangle
        // area, so large triangles contribute proportionally more.
        const float nx = e1y * e2z - e1z * e2y;
        const float ny = e1z * e2x - e1x * e2z;
        const float nz = e1x * e2y - e1y * e2x;

        accum[ia / 8 * 3 + 0] += nx;
        accum[ia / 8 * 3 + 1] += ny;
        accum[ia / 8 * 3 + 2] += nz;
        accum[ib / 8 * 3 + 0] += nx;
        accum[ib / 8 * 3 + 1] += ny;
        accum[ib / 8 * 3 + 2] += nz;
        accum[ic / 8 * 3 + 0] += nx;
        accum[ic / 8 * 3 + 1] += ny;
        accum[ic / 8 * 3 + 2] += nz;
    }

    // The UV sphere duplicates the phi=0 / phi=2pi seam column and the two
    // pole rows. Accumulate their face normals together before normalising,
    // otherwise stepped toon lighting exposes a hard seam along phi=0.
    const auto mergePair = [&](std::size_t a, std::size_t b) {
        for (int k = 0; k < 3; ++k)
        {
            const float sum = accum[a * 3 + k] + accum[b * 3 + k];
            accum[a * 3 + k] = sum;
            accum[b * 3 + k] = sum;
        }
    };
    if (cols >= 2)
        for (std::size_t j = 0; j < rows; ++j) mergePair(j * cols, j * cols + cols - 1);
    if (rows >= 2)
        for (std::size_t poleRow : {std::size_t{0}, rows - 1})
        {
            for (std::size_t i = 1; i < cols; ++i) mergePair(poleRow * cols, poleRow * cols + i);
        }

    for (std::size_t v = 0; v < vertexCount; ++v)
    {
        const float nx = accum[v * 3 + 0];
        const float ny = accum[v * 3 + 1];
        const float nz = accum[v * 3 + 2];
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-12f)
        {
            vertices[v * 8 + 4] = nx / len;
            vertices[v * 8 + 5] = ny / len;
            vertices[v * 8 + 6] = nz / len;
        }
        else
        {
            // Degenerate (pole) vertex: fall back to the radial direction.
            const float px = vertices[v * 8 + 0];
            const float py = vertices[v * 8 + 1];
            const float pz = vertices[v * 8 + 2];
            const float plen = std::sqrt(px * px + py * py + pz * pz);
            if (plen > 1e-12f)
            {
                vertices[v * 8 + 4] = px / plen;
                vertices[v * 8 + 5] = py / plen;
                vertices[v * 8 + 6] = pz / plen;
            }
        }
    }
}

// Generate a deterministic set of craters scattered over the unit sphere.
std::vector<Crater> makeCraters(std::uint32_t seed, int count)
{
    std::vector<Crater> craters;
    craters.reserve(static_cast<std::size_t>(count));
    std::uint32_t rng = seed * 2654435761u + 1u;
    const auto next = [&rng]() {
        rng = hash32(rng);
        return rng;
    };
    for (int i = 0; i < count; ++i)
    {
        // Uniform point on the sphere (inverse-CDF on z).
        const float z = hash01(next()) * 2.0f - 1.0f;
        const float phi = hash01(next()) * 6.2831853f;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        Crater c;
        c.dx = r * std::cos(phi);
        c.dy = z;
        c.dz = r * std::sin(phi);
        // Wider craters are shallower, roughly like real impactors. The
        // lower bound matters: a crater narrower than a couple of mesh
        // cells is invisible once the surface is tessellated, so the
        // smallest craters start wide enough to actually resolve.
        c.radius = 0.30f + hash01(next()) * 0.42f;
        c.depth = 0.07f + hash01(next()) * 0.11f;
        craters.push_back(c);
    }
    return craters;
}



// Must match PushConstants in asteroid.slang (112 bytes).
struct alignas(16) PushConstants
{
    float viewProj[16] = {};
    std::uint64_t vertexBufferAddress = 0;
    std::uint64_t instanceBufferAddress = 0;
    float time = 0.0f;
    alignas(16) float sunDirection[3] = {1.0f, 0.5f, 0.3f};
    std::uint32_t pad = 0;
};

static_assert(std::is_standard_layout_v<PushConstants>, "Asteroid PushConstants must be standard layout");
static_assert(sizeof(PushConstants) == 112, "Asteroid PushConstants must be 112 bytes");
static_assert(offsetof(PushConstants, sunDirection) == 96, "Asteroid sun direction offset must be 96");
static_assert(offsetof(PushConstants, pad) == 108, "Asteroid pad offset must be 108");

} // namespace

AsteroidMesh generateAsteroidMesh(float radius, std::uint32_t latSegments, std::uint32_t lonSegments,
                                  std::uint32_t seed)
{
    AsteroidMesh mesh;
    if (radius <= 0.0f || latSegments == 0 || lonSegments == 0)
    {
        return mesh;
    }

    const std::uint32_t rows = latSegments + 1;
    const std::uint32_t cols = lonSegments + 1;
    mesh.vertices.reserve(static_cast<std::size_t>(rows) * cols * 8);
    mesh.indices.reserve(static_cast<std::size_t>(latSegments) * lonSegments * 6);

    // Three shape layers, matching the reference art:
    //  * large lobes      -> the overall irregular "peanut" silhouette
    //  * mid-frequency    -> lumpy, hand-carved looking bulk
    //  * craters          -> the concave pits with raised rims
    const float sd = hash01(seed + 0x9e3779b9u);
    const float p1 = sd * 6.2831853f;
    const float p2 = sd * 11.1f;
    const float p3 = sd * 3.7f;
    const auto craters = makeCraters(seed, 12);

    // Per-vertex crater depth drives a cheap cavity term that the shader
    // turns into ambient occlusion, so pits read as shadowed hollows instead
    // of flat dimples. The bowl and the rim are tracked separately and
    // combined at the end: summing overlapping craters made a coarse mesh
    // (where one vertex can sit inside three craters at once) collapse into
    // a uniformly black rock.
    std::vector<float> bowlDepth(static_cast<std::size_t>(rows) * cols, 0.0f);
    std::vector<float> rimHeight(static_cast<std::size_t>(rows) * cols, 0.0f);

    const double pi = std::acos(-1.0);
    for (std::uint32_t j = 0; j < rows; ++j)
    {
        const double theta = pi * static_cast<double>(j) / static_cast<double>(latSegments);
        const double sinT = std::sin(theta);
        const double cosT = std::cos(theta);
        for (std::uint32_t i = 0; i < cols; ++i)
        {
            const double phi = 2.0 * pi * static_cast<double>(i) / static_cast<double>(lonSegments);
            const auto nx = static_cast<float>(sinT * std::cos(phi));
            const auto ny = static_cast<float>(-cosT);
            const auto nz = static_cast<float>(sinT * std::sin(phi));

            // Smooth, low-frequency lobes. The old per-direction hash jumped
            // +/-25% between neighbouring vertices, which read as a spiky,
            // jagged silhouette. The seed now only shifts the phase, so the
            // surface stays coherent while each rock keeps a unique shape.
            const float lobe =
                0.60f * std::sin(nx * 3.1f + p1) * std::cos(ny * 2.7f - p2) +
                0.30f * std::sin(ny * 4.3f + nz * 2.1f + p2) +
                0.20f * std::cos(nz * 3.7f - nx * 1.9f + p3);

            // Mid-frequency lumps: value noise sampled on the unit sphere
            // so the detail is seamless across the pole.
            const float lump = fbm3(nx * 2.4f, ny * 2.4f, nz * 2.4f, seed, 3);
            // Fine relief: small, high-frequency bumps that survive the
            // tessellation and break up the remaining flat areas.
            const float fine = fbm3(nx * 8.5f, ny * 8.5f, nz * 8.5f, seed + 977u, 2);

            // Crater profile: a smooth bowl with a raised lip at the rim,
            // the way a real impact crater sits proud of the surface.
            float crater = 0.0f;
            for (const auto& c : craters)
            {
                const float dot = nx * c.dx + ny * c.dy + nz * c.dz;
                // Angular distance from the crater centre.
                const float ang = std::acos(std::clamp(dot, -1.0f, 1.0f));
                if (ang >= c.radius)
                {
                    continue;
                }
                const float t = ang / c.radius;
                // Bowl: deepest at the centre, fading to zero at the rim.
                const float bowl = -c.depth * (1.0f - t * t) * (1.0f - t);
                // Rim: a raised lip just inside the edge.
                const float rimT = (t - 0.78f) / 0.22f;
                const float rimMask = std::max(0.0f, 1.0f - rimT * rimT);
                const float rim = c.depth * 0.42f * rimMask;
                crater += bowl + rim;
                // Signed cavity term: positive inside the pit (ambient
                // occlusion), negative on the raised lip (which catches
                // the light). The shader uses both halves. Take the
                // strongest crater rather than the sum, so overlapping
                // craters cannot pile up into a black rock.
                const std::size_t vi = static_cast<std::size_t>(j) * cols + i;
                bowlDepth[vi] = std::max(bowlDepth[vi], c.depth * (1.0f - t));
                rimHeight[vi] = std::max(rimHeight[vi], c.depth * 0.9f * rimMask);
            }

            const float scale =
                radius * (1.0f + lobe * 0.16f + lump * 0.09f + fine * 0.022f + crater);

            mesh.vertices.insert(mesh.vertices.end(),
                                 {nx * scale, ny * scale, nz * scale, 1.0f, nx, ny, nz, 0.0f});
        }
    }
    for (std::uint32_t j = 0; j < latSegments; ++j)
    {
        for (std::uint32_t i = 0; i < lonSegments; ++i)
        {
            const std::uint32_t a = j * cols + i;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + cols;
            const std::uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    // Recompute normals from the displaced geometry. The old code kept the
    // untouched sphere normal, so the lumps and craters were invisible to
    // the lighting and every rock read as a flat-shaded ball.
    recomputeNormals(mesh.vertices, mesh.indices, rows, cols);

    // Bake the signed cavity term into the unused normal.w slot so the
    // shader can darken crater interiors and brighten the raised rims
    // without another vertex buffer.
    for (std::size_t v = 0; v < static_cast<std::size_t>(rows) * cols; ++v)
    {
        const float cav = std::clamp((bowlDepth[v] - rimHeight[v]) * 2.5f, -1.0f, 1.0f);
        mesh.vertices[v * 8 + 7] = cav;
    }

    return mesh;
}

std::expected<AsteroidFieldRenderer, std::string> AsteroidFieldRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("AsteroidFieldRenderer: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("AsteroidFieldRenderer: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.depthFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("AsteroidFieldRenderer: depthFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("AsteroidFieldRenderer: shaderDirectory is empty");
    }

    AsteroidFieldRenderer renderer;
    renderer.device_ = info.device;

    auto vertModule = internal::loadShaderModule(info.device, info.shaderDirectory + "/asteroid.vert.spv");
    if (!vertModule)
    {
        return std::unexpected(std::move(vertModule.error()));
    }
    auto fragModule = internal::loadShaderModule(info.device, info.shaderDirectory + "/asteroid.frag.spv");
    if (!fragModule)
    {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        return std::unexpected(std::move(fragModule.error()));
    }

    // asteroid.slang's fragment stage reads pc.sunDirection, so the range
    // must expose push constants to both stages.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = static_cast<std::uint32_t>(sizeof(PushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (const VkResult result = vkCreatePipelineLayout(info.device, &layoutInfo, nullptr, &layout);
        result != VK_SUCCESS)
    {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        vkDestroyShaderModule(info.device, *fragModule, nullptr);
        return std::unexpected(vulkan::describeFailure("vkCreatePipelineLayout", result));
    }
    renderer.pipelineLayout_ = layout;

    auto pipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *vertModule,
        .fragmentShader = *fragModule,
        .pipelineLayout = renderer.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_LESS,
        .backfaceCulling = false,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    vkDestroyShaderModule(info.device, *vertModule, nullptr);
    vkDestroyShaderModule(info.device, *fragModule, nullptr);
    if (!pipeline)
    {
        vkDestroyPipelineLayout(info.device, renderer.pipelineLayout_, nullptr);
        renderer.pipelineLayout_ = VK_NULL_HANDLE;
        return std::unexpected(std::move(pipeline.error()));
    }
    renderer.pipeline_ = std::move(*pipeline);

    return renderer;
}

AsteroidFieldRenderer::~AsteroidFieldRenderer()
{
    destroy();
}

AsteroidFieldRenderer::AsteroidFieldRenderer(AsteroidFieldRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt))
{
}

AsteroidFieldRenderer& AsteroidFieldRenderer::operator=(AsteroidFieldRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, std::nullopt);
    }
    return *this;
}

void AsteroidFieldRenderer::destroy() noexcept
{
    pipeline_.reset();
    if (device_ != VK_NULL_HANDLE && pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    pipelineLayout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

void AsteroidFieldRenderer::render(VkCommandBuffer cmd, const AsteroidFieldRenderContext& context, const AsteroidFieldParams& params)
{
    if (!pipeline_.has_value())
    {
        return;
    }

    PushConstants push{};
    std::memcpy(push.viewProj, glm::value_ptr(context.viewProj), sizeof(push.viewProj));
    push.vertexBufferAddress = params.vertexBufferAddress;
    push.instanceBufferAddress = params.instanceBufferAddress;
    push.time = params.time;
    push.sunDirection[0] = context.sunDirection.x;
    push.sunDirection[1] = context.sunDirection.y;
    push.sunDirection[2] = context.sunDirection.z;

    vkCmdPushConstants(cmd, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, static_cast<std::uint32_t>(sizeof(push)), &push);

    // Caller must bind the index buffer before render() (params carry
    // addresses for the shader, not the VkBuffer handle).
    pipeline_->bindAndDraw(cmd, 0, params.indexCount, params.instanceCount);
}

} // namespace starforge::environment
