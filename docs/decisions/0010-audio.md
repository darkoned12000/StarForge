# ADR-0010: Audio layer choice and headless strategy

- **Status:** Accepted (M12)
- **Context:** M12 requires 3D positional audio, music, voice, and UI
  sounds. The game-layer stack names miniaudio; no audio code exists
  yet. Tests must run on headless CI without audio hardware, and
  emitter/listener coordinates must obey the same camera-relative
  single-cast rule as rendering (M3) and physics (M11).
- **Decision:**
  - Integrate [miniaudio](https://github.com/mackron/miniaudio)
    v0.11.26 via CMake `FetchContent` (pinned tag), behind a new
    `STARFORGE_USE_MINIAUDIO` option — the same pattern as Flecs (M10)
    and Jolt (M11).
  - Wrap it in an internal `starforge_audio` library (`src/audio/`)
    exposing RAII, move-only `AudioContext` (`ma_context` +
    `ma_engine`) and `SoundBank` (decoded `ma_sound` voices routed
    through per-bus `ma_sound_group`s). Fallible construction returns
    `std::expected` per the project error-handling convention.
  - Headless strategy: a `Backend::Null` configuration forces
    `ma_backend_null` so creation, decoding, and playback run with no
    audio hardware. `Backend::Default` selects OS backends for the
    sandbox and games.
  - No miniaudio types in `include/starforge/` (ADR-0009 holds; the
    public facade arrives in M12.5). `miniaudio.h` is included only
    in `.cpp` files; headers forward-declare `ma_context`/`ma_engine`/
    `ma_sound`/`ma_decoder`.
  - Public facade: `starforge::Audio` in `include/starforge/audio.hpp`
    (move-only, `std::expected` factory, camera-relative
    listener/emitter, per-voice volume/pitch/loop). The bridge
    (`src/public_api/audio_bridge`) uses plain data because public
    (`starforge/math.hpp`, GLM-free) and internal (`src/math/math.hpp`,
    GLM) math headers cannot coexist in one translation unit; the
    internal types are confined to the bridge implementation file.
  - UI and Music are non-spatial buses (spatialization explicitly
    disabled per sound); SFX and Voice are spatial.
  - Spatialization model: engine listener 0 in absolute positioning
    space; all poses are camera-relative float32 (single-cast rule).
    The CPU reference model in `src/audio/spatial.hpp` (inverse-
    distance-clamped gain with a positive floor past max range,
    stereo pan, Doppler with supersonic saturation at 2.0) is the
    contract; the miniaudio spatializer must match it (tests assert
    the round-trip through the engine).
  - ECS bridge: `AudioEmitter`/`AudioListener` components live in
    `src/audio/bridge.hpp` (physics-style layering, no audio types in
    `src/ecs/components.hpp`). One-shot triggers flow through a
    mutex-guarded `TriggerQueue` flushed on the main thread after the
    ECS step — the audio thread never mutates Flecs (M11 pattern).
  - Lifetime: a `SoundBank` borrows its `AudioContext` and must be
    destroyed first (declare it after the context). `AudioContext`
    tracks live banks and asserts none remain at teardown. Moving an
    `AudioContext` while banks borrow it is asserted (debug) — banks
    hold a raw pointer to the context object.
- **Consequences:**
  - Testable in CI without a sound card; real-hardware behaviour is
    proven in the sandbox (T5).
  - Asset loading/streaming stays out of M12 and is delegated to M17.
  - Upgrading miniaudio is a one-line pinned-tag bump, as with
    Flecs/Jolt.
  - Sounds retain their encoded bytes (`ma_decoder_init_memory` reads
    lazily from the backing buffer); callers need not keep buffers
    alive after `loadFromMemory` returns.
