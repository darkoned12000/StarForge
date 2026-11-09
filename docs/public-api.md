# STARFORGE Public API

The public API is the set of headers under `include/starforge/`, built as
the `starforge_public_api` CMake target (also linked into the umbrella
`starforge` target). It is backend-neutral: public headers never mention
Vulkan, VMA, SDL, or GLM, and consumers need no internal `src/` headers.
ADR-0009 (`docs/decisions/0009-public-api-contract.md`) records the
contract; the sandbox in `apps/sandbox/` is the reference example.

## Quick start

```cpp
#include "starforge/application.hpp"
#include "starforge/math.hpp"
#include "starforge/renderer.hpp"
#include "starforge/starfield.hpp"

int main()
{
    auto app = starforge::Application::create({
        .title = "My Game", .width = 1280, .height = 720});
    if (!app) return 1;

    auto renderer = starforge::Renderer::create(*app, {});
    if (!renderer) return 1;

    auto starfield = starforge::Starfield::create(*renderer, {});
    if (!starfield) return 1;

    float time = 0.0f;
    while (!app->shouldClose()) {
        app->pollEvents();
        if (app->takeResized()) {
            if (auto r = renderer->recreate();
                r && *r == starforge::Renderer::RecreateResult::FormatChanged) {
                starfield->recreate(*renderer);
            }
        }
        if (app->isMinimized()) continue;

        const auto size = app->framebufferSize();
        const float aspect = float(size.width) / float(size.height);

        starforge::Starfield::RenderContext context;
        context.viewProj =
            starforge::math::perspectiveVulkan(1.0f, aspect, 0.1f, 1000.0f) *
            starforge::math::lookAt({0, 0, 5}, {0, 0, 0}, starforge::math::kUpY);
        context.invViewProj = starforge::math::inverse(context.viewProj);
        context.time = time += 0.016f;

        starforge::Starfield::Params params;
        params.worldPosition = {0, 0, -50};
        params.radius = 8.0f;

        auto frame = renderer->renderFrame(
            context.clearColor, [&](starforge::FrameContext& frameContext) {
                starfield->record(frameContext, context, params);
            });
        if (!frame) return 1;
    }
    renderer->waitIdle();
    return 0;
}
```

Multiple objects can record into one frame by calling their `record()`
methods inside a single `Renderer::renderFrame` callback; presentation
happens once per frame. `Starfield::render()` is a single-object
convenience wrapper around the same path.

## Types

- `starforge::Application` — window and platform lifecycle (`create`,
  `pollEvents`, `shouldClose`, `takeResized`, `framebufferSize`,
  `isMinimized`, `title`), plus `starforge::Size2D`.
- `starforge::Renderer` — swapchain, frame submission, resize, and
  presentation. `renderClearedFrame`, `renderFrame`, `recreate`,
  `extent`, `waitIdle`. `FrameContext` is the opaque per-frame record
  context (exposes only `extent()`).
- `starforge::Starfield` — procedural starfield and sun billboard.
  Scene-level parameters only; shader artifacts are bundled.
- `starforge::Toon` — cel-shaded (toon) mesh renderer: quantized
  diffuse bands, stepped specular, rim light, and an inverted-hull
  outline pass. `record()`/`render()`/`recreate()` mirror `Starfield`;
  outline thickness is specified in screen pixels. Diffuse color can use a
  three-stop ramp (`shadowColor` → `midColor` → `baseColor`); a `midColor`
  with alpha 0 uses the midpoint of the shadow and base colors.
- `starforge::Audio` — backend-neutral audio: `create`, `loadFromMemory`
  (WAV/FLAC/MP3 bytes), `play`/`stop`/`isPlaying`, per-bus gain
  (`setBusVolume`), `setListener`, `setEmitter` (3D positional audio with
  Doppler factor), `setEmitterAttenuation`, master volume. `Backend::Silent`
  needs no audio hardware (headless tests); `Backend::Default` uses OS
  devices (sandbox flyby). `generateToneWav` provides placeholder sine
  WAVs without binary assets.
- `starforge::math` — GLM-free `Vec2/3/4f/d`, `Mat3f/d`, `Mat4f/d`,
  arithmetic, `dot`/`cross`/`length`/`normalize`, `perspectiveVulkan`,
  `perspectiveVulkanReverseZ`, `lookAt`, `inverse`, `toFloat32`.
  Conventions: right-handed, Y-up, camera looks down −Z, Vulkan clip
  depth [0, 1] (Y-flip via negative viewport, handled by the backend).
- `starforge::WorldPosition` and helpers (`toCameraRelative`,
  `composeCameraRelativeModelMatrix`, `LargeObjectSetup`) for
  precision-preserving world coordinates. See `include/starforge/world.hpp`.

## Ownership and lifetime

- `Application`, `Renderer`, and `Starfield` are **move-only**; copying
  is deleted. Destruction releases all owned resources. `Audio` is
  move-only for the same reason; a moved-from `Audio` is inert (queries
  return safe defaults, operations return false or an expected error).
- A `Renderer` borrows its `Application`; a `Starfield` borrows its
  `Renderer`. The borrowed object must outlive the borrower.
- Call `Renderer::waitIdle()` before destroying or move-assigning a
  renderer or starfield that has recorded frames. Destruction and move
  assignment also wait defensively, but explicit idle is cheaper to
  reason about.
- A moved-from instance is inert: query-style methods return safe
  defaults and operations that would need the backend return an error.
- After `Renderer::recreate()` returns `FormatChanged`, recreate
  format-dependent facades (e.g. `Starfield::recreate()`); `Resized`
  requires nothing. Avoid recreating facades on every resize.

## Errors

Fallible operations return `std::expected<T, std::string>`; the string
is a human-readable diagnostic (for example, a missing shader file or a
moved-from renderer). Frame submission distinguishes expected control
flow (`Renderer::FrameResult::SwapchainOutOfDate`, returned as a value)
from real errors (returned as `std::unexpected`). Nothing in the public
API throws across the boundary for ordinary failures.

## Threading

The public API is not thread-safe by default: create, record, and
destroy objects on one thread (the render thread), in that order.
`WorldPosition`, math value types, and the pure math functions are
stateless and safe to use anywhere. Internal parallelism (job system,
command pools) is a backend concern and stays hidden.

## Configuration and shader artifacts

- Minimum build: CMake 3.30+, C++26 compiler, Vulkan SDK 1.4,
  SDL3 (windowing; linked privately by the public API target is not
  required of consumers — link `starforge_public_api` and include only
  `include/`).
- Debug builds enable Vulkan validation layers by default.
- Bundled shaders are resolved at runtime: explicit
  `Starfield::CreateInfo::shaderDirectory`, then the
  `STARFORGE_SHADER_DIR` environment variable, then directories
  relative to the running executable (`shaders/`, `../shaders/`,
  `../share/starforge/shaders/`), then the build-tree shader directory
  (development fallback). Ship the `.spv` files alongside your
  executable or set the environment variable. Embedded SPIR-V is a
  planned packaging improvement, not yet implemented.

## Stability policy (pre-1.0)

The public API is **unstable before 1.0**: minor versions may add,
rename, or remove public types, functions, and fields. Facades are
added only when exercised by the public example, so expect growth
(new effects, resources) more than churn, but do not pin against
internal build details. Breaking changes will be noted in the
roadmap when they land. The backend-neutrality guarantee (no Vulkan,
VMA, SDL, GLM, or miniaudio symbols in public headers) and the
ownership/error rules above are the stable contract; the leakage gate
in the test suite enforces them.
