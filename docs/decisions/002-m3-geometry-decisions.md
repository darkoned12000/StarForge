# ADR-002: M3 Geometry and Precision Decisions

- **Status:** Accepted
- **Date:** 2026-11-03
- **Target milestone:** M3

## Context

M3 proves the central claim: accurate positioning across astronomical
distances, end-to-end through the GPU pipeline. M2 proved the math (CPU side);
M3 must prove it through vertex buffers, pipelines, and dynamic rendering.

Several design decisions must be resolved before implementation begins. These
affect the rest of the project.

## Decisions

### 1. Camera and mesh data path: push constants + buffer device address (BDA)

**Decision:** Use push constants for per-draw data (camera-relative model
matrix as 4×4 float = 64 bytes, plus material index as uint32 = 4 bytes,
totalling 68 bytes — fits within Vulkan's guaranteed 128-byte minimum
push-constant capacity) and buffer device address (BDA) for vertex/index
data via shader-side geometry access (buffer references in shaders, not
conventional vertex binding). No descriptor sets for mesh data. No
world-space floats or separate model/view matrices are ever uploaded.

**Rationale:** M5 needs BDA for GPU-driven rendering. Building a descriptor-set
pattern now would be thrown away. Push constants are sufficient for per-draw
data and avoid descriptor pool management for M3.

**BDA implementation details:**
- Required Vulkan feature: `bufferDeviceAddress` (already a hard requirement).
- Required buffer usage: `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`.
- VMA allocator configured with `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`
  (already enabled in M1.5).
- Device-address alignment requirements respected.
- Shader-side buffer-reference declarations and layout compatibility verified.
- Lifetime guarantees: a referenced buffer must remain alive while GPU work
  can access its address.

**Note:** BDA is chosen for geometry only. Descriptors may still be used for
other resources later (textures, bindless tables in M5).

**Consequences:**
- The `VkPipelineLayout` uses push constant ranges, not descriptor set layouts.
- Vertex data is accessed via `bufferDeviceAddress` (already a hard requirement).
- Camera-relative Model-View matrices are computed on the CPU and passed as
  push constants.

### 1a. GPU coordinate system boundary (core design rule)

**Decision:** The CPU owns astronomical world coordinates; the GPU owns
camera-relative rendering coordinates. Absolute astronomical world positions
are never uploaded to the GPU. World-to-camera-relative conversion is
performed using STARFORGE's high-precision CPU spatial system. GPU-facing
transforms contain only camera-relative coordinates. The renderer's
camera/projection data contract is defined separately from the
world-position representation.

### 2. Image format and colour space

**Decision:** RGBA8 UNORM for offscreen comparison (separate from the
swapchain's sRGB). Depth: D32_SFLOAT preferred. For the astronomical-depth
proof, a floating-point depth format is **required** — D24_UNORM_S8_UINT is
a compatibility fallback for ordinary rendering only and does not provide
the same reverse-Z precision benefits (fixed-point depth loses precision at
extreme distances). Format support queried at runtime via
`findDepthFormat()`. Selected format is logged and recorded in test output.

**Rationale:** UNORM avoids sRGB conversion complexity in golden-image
comparison. D32_SFLOAT gives the best depth precision for reverse-Z.
D24_UNORM_S8_UINT is tested as a compatibility format but not for
astronomical precision.

### 3. Golden image policy

**Decision:** PNG via stb_image_write (write) and stb_image (read) — both
vendored as single headers in `third_party/`. Per-channel tolerance plus
maximum failing-pixel fraction. Small images (256×256). Comparison details:
alpha is compared; comparison occurs in the image's native space (UNORM, no
sRGB conversion for offscreen targets); a pixel fails if any channel exceeds
tolerance; failing-pixel fraction calculated across the entire image; image
dimensions must match exactly. Golden-update workflow: normal test failure
writes actual and diff images to the build dir; explicit golden update
(`STARFORGE_UPDATE_GOLDENS=ON`) generates replacement reference images but
never silently overwrites existing goldens during an ordinary test run.
Anti-aliasing is off for all test renders (deterministic pixel output).

**Note:** Disabling MSAA does not guarantee identical output across GPU
vendors, drivers, or shader compiler versions. Goldens are validated on the
target GPU (RX 7900 XTX) and tolerances account for minor rasterization
differences. stb_image_write only writes PNG files; stb_image is needed for
reading existing goldens.

**Rationale:** PNG is universally viewable and diffable. Per-channel tolerance
accounts for driver/GPU rasterization differences. The update workflow makes
golden failures debuggable. Separating diagnostic output from golden update
protects against accidentally accepting a broken renderer.

### 4. Anti-aliasing

**Decision:** Off for all test renders. Deterministic pixel output.

### 5. Matrix layout

**Decision:** Slang column-major, matching GLM. Set explicitly via compiler
flag (`-matrix-layout-column-major`) and tested with a non-symmetric matrix.

### 6. Entry-point naming

**Decision:** Slang shaders use unique entry-point names per stage:
`vertexMain` for vertex shaders, `fragmentMain` for fragment shaders,
`computeMain` for compute shaders. The `starforge_add_shader` helper accepts
the entry point and stage as arguments. The compute dispatch fallback
(`main` then `computeMain`) is removed — all shaders use consistent
stage-specific names. This avoids the conditional compilation complexity of
using `main` for multiple stages in the same source file.

### 7. Depth buffer and reverse-Z

**Decision:** Clear depth to 0, compare with `GREATER`, viewport depth range
0..1, infinite-far projection. Reverse-Z details documented in `math.hpp`
(`perspectiveVulkanReverseZ`, `perspectiveVulkanReverseZInfinite`).

**Physical limit:** Reverse-Z with D32_SFLOAT gives about 6e-8 relative depth
resolution. At 1e9 m, this resolves separations of roughly 60–100 m. Surfaces
closer than that will still z-fight. This is a hardware limit, not a bug.

### 8. Orientation and winding

**Decision:** Asymmetric test shapes with per-face colors on the cube. Winding
order verified in tests. Y-flip handled via projection (negate Y scale in
projection matrix), not via negative-height viewport.

### 9. Frame recording

**Decision:** M3 builds on the frame and barrier code from 1.8. The sandbox
presents cleared frames with depth. Offscreen T3 tests use `FrameRenderer`'s
`renderClearedFrame` infrastructure extended for depth and color attachments.