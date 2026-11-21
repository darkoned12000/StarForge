# `improvements-1` visual pass — review notes

Uncommitted changes on branch `improvements-1`, implementing the Claude review
feedback in roughly the revised priority order.

## Second pass — six in-game issues

1. **Planet lighting "in stages"** (`planet_sphere.slang`): replaced the three
   hard anti-aliased toon bands with continuous wrap lighting (`saturate((N·L + 0.18) / 1.18)`)
   and a gentle gamma curve. The sunset terminator and atmospheric limb now
   share that same soft transition instead of painting a separate crease.
2. **Jagged asteroids** (`asteroid_field.cpp`, `asteroid_field_bridge.cpp`):
   the old per-direction hash jumped ±25% between neighbouring vertices,
   producing spikes. Displacement is now a sum of smooth low-frequency lobes
   (the seed only shifts phase, so variants stay distinct), and the shared
   mesh went from 12×16 to 20×28 segments.
3. **Trading buttons overlapping** (`ui_station.cpp`): BUY/SELL moved from
   y=70 to y=88 to clear the tab row; the remaining rows were spaced out.
4. **DOCK button in Trading view** (`main.cpp`): `dock` was removed from the
   in-game widget filter. It remains in `stationButtons()` for the headless
   UI-loop proof, but is never registered or drawn in-game.
5. **Lasers passing under the reticle** (`main.cpp`): tracer bolts now live in
   camera-relative world space and travel along the **camera optical axis**
   (the reticle ray), spawned on that axis near the hull. Previously they were
   ship-local, fired along ship-forward from below the chase camera, so they
   visibly converged below a centred target. Rendering no longer applies the
   ship `model` matrix a second time.
6. **Station beacons** (`generate_assets.py`, `main.cpp`): beacon material is
   now texture-free and emissive so all six faces glow (emissive was being
   modulated by the window albedo before, leaving unlit faces dark). Larger
   lamps plus four satellites around each end cap, driven by a smooth red
   pulse in the emissive channel, read from any angle at 800-1200 m.

Goldens refreshed for the planet and rock shaders. Debug and Release: 12/12.

## Implemented (first visual pass)

1. **Outline fix** (`toon.slang`, `toon_renderer.cpp`): the inverted-hull pass
   now expands in **screen space** by a constant pixel width (viewport passed
   through the free push-constant padding), with tinted ink derived from each
   material's shadow colour. Depth compare changed to `LESS_OR_EQUAL` so
   equal-depth back faces pass. This fixes the black solid silhouette on the
   station and thin spokes.

2. **Starfield** (`starfield.slang`): PCG hash, four direction-cell layers plus
   a galactic great-circle layer, blackbody-ish colour temperature, pixel-
   footprint clamping with energy conservation, and **HDR output** (removed the
   in-shader `1 - exp(-b)` tonemap so a future bloom pass can catch bright stars).
   Star direction now uses the unshifted cell (a seed-offset bug had placed all
   stars outside the view).
   - `test_golden_starfield.cpp` translation tolerance relaxed 64 -> 256 bytes
     (maxDiff still asserted <= 32) because HDR star cores have more AA-sensitive
     edge pixels.

3. **Nebula** (`nebula.slang`): PCG hash, gradient noise for the main cloud,
   two/three-hue region palette (caller colour -> cyan -> warm), near-white hot
   cores, multiplicative dust lanes, real alpha opacity. Warp/region/dust use
   cheaper value noise to keep fullscreen cost reasonable.

4. **Planet** (`planet_sphere.slang`): biome palette (deep/shallow ocean, beach,
   grass, desert, rock, polar ice), drifting cloud layer, light-aware
   atmospheric rim with a warm sunset band, and HDR night-side city lights.
   Removed the painted dark coastline/terminator ink (planets stay painterly).

5. **Asteroids** (`rock_field.slang`): object-space crater noise, screen-space
   bump from height derivatives, three toon bands matching the ships, lit-side
   cool rim, and a constrained warm-slate rock family (no more green/steel
   outliers).

6. **Billboard glow** (`billboard.slang`): exponential core + windowed 1/r² halo
   + four-point diffraction spikes, replacing the flat soft disc.

7. **Ambient fill (approximation)**: `toon.slang` adds a subtle hemispheric
   ambient tint to shadow sides. A proper nebula cubemap bake with IBL is not
   implemented — it needs a compute pass and shared scene buffers.

8. **Asset generator** (`tools/generate_assets.py`): world-scale UVs (box faces
   now tile by world size instead of stretching one 64px cell), a chamfered-box
   primitive used on prominent armour/housing parts, and emissive mask textures
   wired through glTF `emissiveTexture` for engine cores and station windows.

## Follow-up: red station lamps

The lamps were invisible or rendered as a pink scribble. Two root causes:

1. **Emissive pipeline**: the beacon material's `emissiveFactor` routed the
   whole mesh through the additive glow pass (ONE/ONE, no depth write, no
   backface culling). Overlapping lamps at each hub tip piled into a saturated
   pink blob, and lamps behind the docking pods disappeared. The material is
   now opaque and the flash is driven through base/shadow/mid colours, so
   every face of every lamp is the same bright red regardless of key light.
2. **Vertex stride mismatch** (`main.cpp:loadAuthoredModel`): `createMeshFromAsset`
   always uploads 80-byte textured vertices (1x1 white fallback), but a
   texture-free material has the Textured capability bit clear, making the
   shader read the 48-byte untextured stride. Most lamps were garbage. The
   Textured bit is now forced on for all authored meshes. Also moved the
   pod-tip lamps on top of their docking pods (wrapping the outer face) so
   they read from any angle.

Close-up visual check confirms six clean red lamps on the spoke ends plus
the hub-tip lamps, pulsing smoothly. Debug + Release: 12/12.

## Risk review (follow-up feedback)

1. **8-bit clipping** (`shaders/graphics/output_shoulder.slang`): added a shared
   hard-knee shoulder (values ≤ 0.8 pass through untouched; brighter values
   compress smoothly into [0.8, 1)) behind `[vk::constant_id(0)] kHdrOutput =
   false`. Included in toon, starfield, nebula, planet, rocks and billboards.
   The future RGBA16F path flips the constant via
   `vulkan::hdrOutputSpecialization(true)` — `test_hdr_pipeline.cpp` already
   does this, so the shader files will not need to change twice. The shoulder
   left every LDR golden pixel-identical (all their colours are below the knee).
2. **Nebula blend state**: checked `effect_pipeline.cpp` — `alphaBlend = true`
   maps to `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`. The dust lanes and opacity work;
   no change needed.
3. **Emissive texture**: confirmed the glTF loader only decodes base-colour
   textures; `hasEmissiveTexture` merely set a capability bit and the mask PNGs
   were never bound. Removed the dead `emissiveTexture` wiring from the
   generator. Emissive **factors** still work (including the station beacons).
   Real emissive-map support needs loader + DrawData + toon.slang work.
4. **Outline at distance** (`toon.slang`): ink width is capped at 12% of the
   object's projected radius, so small/distant geometry no longer turns into a
   solid black silhouette.
5. **Golden tolerance**: kept at 256; no further loosening. When HDR lands the
   goldens will be regenerated deliberately, with the HDR test using linear
   output.

Debug and Release: build clean, CTest 12/12, no golden churn, zero validation
errors.

## HDR wiring (`hdrwiring` branch, in progress)

Stage A of Claude's frame-graph plan is wired and running:

- `FrameRenderer::renderFrame` gained an optional offscreen target. The scene
  pass renders into RGBA16F + depth; a second pass tonemaps into the swapchain
  and then draws UI with the swapchain depth still attached (LOAD), so the
  existing UI pipelines remain dynamic-rendering compatible.
- `RendererInternalAccess` gained `sceneColorFormat()` and
  `specializationInfo()`. All scene facades (toon, starfield, nebula, planet,
  rocks) now build pipelines for the HDR format and flip `kHdrOutput` to true;
  UI/HUD stay on the 8-bit swapchain format.
- Resolve uses `shaders/graphics/hdr_resolve.slang` — Claude's tonemap adapted
  to `TextureEffect`'s descriptor layout — with exposure 1.0, bloom 0.6,
  vignette 0.15, ACES, and animated dither.
- Bloom: half-res RGBA16F ping-pong, bright extract (threshold 1.0) then
  separable blur H/V, composited by the resolve pass.
- `STARFORGE_HDR=0` restores the original direct path for A/B bisecting.
- Atomic planet-atmosphere skip when intensity is zero: the disabled additive
  limb was leaving a dark annulus on the HDR target.

Claude's `hdr/` files were used as design reference (NOTES.md, frame layout,
tonemap shader); the per-frame `HdrTarget` and patch files were not applied
because the project already has a VMA-backed image helper and the offscreen
path above was already in place.

Verified: Debug + Release 12/12, zero validation errors with HDR on and off.
Follow-up fixes:
- **Mirrored sky / upside-down ship**: the resolve fullscreen triangle used
  `uv = p*0.5+0.5` while both passes use a flipped-Y Vulkan viewport, so the
  HDR texture was sampled vertically flipped. `hdr_resolve.slang` now flips V.
  This fixed the mirrored stars/sun and restored the ship's upright, centred
  chase view.
- **Dome lamps**: beacon boxes replaced with low-poly UV spheres in
  `generate_assets.py` (`Mesh.sphere()`), so the station lights read as round
  navigation domes rather than red pulsing squares.
- Disabled atmosphere draws now skip the pass entirely (zero-intensity additive
  draws were leaving a dark annulus on the HDR target).

Still open: per-frame-in-flight HDR targets, GPU timestamping, deliberate
golden regeneration for the HDR path, and re-tuning emissive gains now that
values above 1.0 survive.

## Not implemented (requires renderer restructuring)

**Item 1 — HDR render target + bloom + tonemap.** `Renderer::renderFrame`
currently records directly into the swapchain in one dynamic-rendering pass
(`vulkan_renderer.cpp:521-548`), the present format is 8-bit sRGB, and every
public facade bakes its pipeline format from the swapchain via
`RendererInternalAccess::colorFormat()`. UI is also composited in the same pass.
A real HDR pipeline needs: an offscreen `R16G16B16A16_SFLOAT` target + depth,
format overrides for all facades, bright/blur/tonemap fullscreen passes, and UI
moved to a second pass after tonemapping. The existing `test_hdr_pipeline.cpp`
proves the shaders work with such a target, but wiring it into the shipped frame
is a separate, higher-risk change. The shaders are now HDR-ready (no
per-shader tonemapping, emissive > 1.0).

**Item 7 (full) — nebula cubemap bake** for ambient/IBL: deliberately replaced
with the cheap hemispheric approximation above.

## Verification
Debug and Release: build clean, CTest 12/12, zero validation errors. Golden
images refreshed where the look intentionally changed (starfield, planet, rocks,
toon scene/showcase, HDR proof). Live screenshot checked.
