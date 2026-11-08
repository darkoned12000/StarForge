#pragma once

// Audio context (M12): RAII owner of a miniaudio ma_context + ma_engine.
// The engine owns its playback device internally (started on creation,
// stopped on destruction without separate device management); the
// context is move-only and fallible construction returns
// std::expected (per the project error-handling convention).
//
// miniaudio.h is NOT included here: it is a ~70k-line single header
// that would slow every TU touching this surface. The C structs are
// forward-declared; <miniaudio.h> is included only in the .cpp files.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "math/math.hpp"

struct ma_context;
struct ma_engine;

namespace starforge::audio
{

// Custom deleters for the engine/context: default-constructible
// (so unique_ptr members default-init cleanly) and defined in
// audio_context.cpp where the C structs are complete. Using these
// keeps <miniaudio.h> out of this header (forward-declaration seam).
struct MaContextDeleter
{
    void operator()(ma_context* context) const noexcept;
};
struct MaEngineDeleter
{
    void operator()(ma_engine* engine) const noexcept;
};
using MaContextHandle = std::unique_ptr<ma_context, MaContextDeleter>;
using MaEngineHandle = std::unique_ptr<ma_engine, MaEngineDeleter>;

// Playback buses. Master is the engine-level gain; the remaining buses
// each own a ma_sound_group in SoundBank.
enum class Bus : std::uint8_t
{
    Master = 0,
    Music = 1,
    SFX = 2,
    UI = 3,
    Voice = 4,
};

class AudioContext
{
public:
    struct Config
    {
        std::uint32_t sampleRate = 48000;
        std::uint32_t channels = 2;
        enum class Backend
        {
            Default, // Real hardware via OS backends (sandbox, games).
            Null,    // ma_backend_null: no audio hardware (headless CI, tests).
        };
        Backend backend = Backend::Null;
    };

    // Listener 0 (the player). Vectors are camera-relative float32 in
    // the engine's absolute positioning space.
    [[nodiscard]] bool setListenerPose(const math::Vec3f& position, const math::Vec3f& velocity,
                                       const math::Vec3f& forward, const math::Vec3f& up) noexcept;
    [[nodiscard]] math::Vec3f listenerPosition() const noexcept;
    [[nodiscard]] math::Vec3f listenerVelocity() const noexcept;
    [[nodiscard]] math::Vec3f listenerDirection() const noexcept;

    [[nodiscard]] static std::expected<AudioContext, std::string> create(const Config& config);

    AudioContext() noexcept;
    ~AudioContext();

    AudioContext(const AudioContext&) = delete;
    AudioContext& operator=(const AudioContext&) = delete;
    AudioContext(AudioContext&& other) noexcept;
    AudioContext& operator=(AudioContext&& other) noexcept;

    [[nodiscard]] bool isValid() const noexcept { return initialized_; }
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept;
    [[nodiscard]] const char* backendName() const noexcept;

    // Master (engine-level) gain in [0, 1]; out-of-range input is clamped.
    void setMasterVolume(float volume) noexcept;
    [[nodiscard]] float masterVolume() const noexcept { return masterVolume_; }

    // Internal handles (this layer only; never exported via include/).
    // Valid only when isValid() is true.
    [[nodiscard]] ma_engine* engineHandle() noexcept { return engine_.get(); }
    [[nodiscard]] const ma_engine* engineHandle() const noexcept { return engine_.get(); }

    // Lifetime guard (internal): SoundBank registers on create and
    // unregisters on destruction. The context asserts no banks are
    // alive when it tears the engine down, so declaration order
    // (bank declared after context => destroyed before it) is enforced
    // instead of silently crashing on use-after-uninit.
    void registerBank() noexcept;
    void unregisterBank() noexcept;
    [[nodiscard]] std::size_t liveBankCount() const noexcept { return liveBanks_; }

private:
    void teardown() noexcept;

    // miniaudio structs embed mutexes and must never be value-copied:
    // heap-owned, move transfers the pointers.
    MaContextHandle context_;
    MaEngineHandle engine_;
    bool initialized_ = false;
    float masterVolume_ = 1.0f;
    Config::Backend backend_ = Config::Backend::Null;
    std::size_t liveBanks_ = 0;
};

} // namespace starforge::audio
