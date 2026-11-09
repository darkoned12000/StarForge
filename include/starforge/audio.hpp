#pragma once

#include "starforge/math.hpp"
#include "starforge/world.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace starforge
{

// Public audio facade: backend-neutral 3D positional audio, music,
// voice, and UI sounds. Scene-level concerns only: caller supplies
// decoded audio bytes, world positions, and velocities; the backend
// owns the engine, context, decoding, and spatialization. No backend
// types are visible through this header (ADR-0009).
class Audio
{
public:
    enum class Bus : std::uint8_t
    {
        Master = 0,
        Music = 1,
        SFX = 2,
        UI = 3,
        Voice = 4,
    };

    struct CreateInfo
    {
        enum class Backend
        {
            Default, // Real hardware via OS backends (sandbox, games).
            Silent,  // No audio hardware (headless tests, CI).
        };
        Backend backend = Backend::Silent;
        std::uint32_t sampleRate = 48000;
        std::uint32_t channels = 2;
    };

    struct VoiceHandle
    {
        static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFU;
        std::uint32_t index = kInvalidIndex;
        [[nodiscard]] bool isValid() const noexcept { return index != kInvalidIndex; }
        friend bool operator==(const VoiceHandle&, const VoiceHandle&) noexcept = default;
        friend auto operator<=>(const VoiceHandle&, const VoiceHandle&) noexcept = default;
    };

    struct SoundParams
    {
        Bus bus = Bus::SFX;
        bool loop = false;
        float volume = 1.0f;
        float pitch = 1.0f;
        float rolloff = 1.0f;
        float minDistance = 1.0f;
        float maxDistance = 1000.0f; // Distance clamp; gain floors here, never silences.
    };

    struct ListenerState
    {
        WorldPosition position;
        math::Vec3d velocity{};
        math::Vec3f forward{0.0f, 0.0f, -1.0f};
        math::Vec3f up{0.0f, 1.0f, 0.0f};
    };

    struct EmitterState
    {
        WorldPosition position;
        math::Vec3d velocity{};
    };

    [[nodiscard]] static std::expected<Audio, std::string> create(const CreateInfo& info);

    ~Audio();
    Audio(Audio&& other) noexcept;
    Audio& operator=(Audio&& other) noexcept;
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    [[nodiscard]] bool isValid() const noexcept { return impl_ != nullptr; }
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept;

    void setMasterVolume(float volume) noexcept;
    [[nodiscard]] float masterVolume() const noexcept;

    // Decodes in-memory audio data (WAV/FLAC/MP3) and creates a stopped
    // voice. The bytes are copied; the caller need not keep them alive.
    [[nodiscard]] std::expected<VoiceHandle, std::string> loadFromMemory(std::span<const std::uint8_t> data);
    [[nodiscard]] std::expected<VoiceHandle, std::string>
    loadFromMemory(std::span<const std::uint8_t> data, const SoundParams& params);

    bool play(VoiceHandle voice, bool restartIfPlaying = false) noexcept;
    bool stop(VoiceHandle voice) noexcept;
    [[nodiscard]] bool isPlaying(VoiceHandle voice) const noexcept;
    bool setVolume(VoiceHandle voice, float volume) noexcept;
    bool setPitch(VoiceHandle voice, float pitch) noexcept;
    bool setLoop(VoiceHandle voice, bool loop) noexcept;
    [[nodiscard]] float volume(VoiceHandle voice) const noexcept;
    [[nodiscard]] float pitch(VoiceHandle voice) const noexcept;
    [[nodiscard]] bool isLooping(VoiceHandle voice) const noexcept;

    // Listener 0 pose, relative to the render camera. Vectors are
    // camera-relative float32 by the time they reach the backend.
    void setListener(const ListenerState& state, const WorldPosition& camera) noexcept;

    // Emitter pose relative to the render camera. 2D buses (UI/Music)
    // reject with false: they are non-spatial by design. The Doppler
    // factor defaults to leaving the voice's configured factor
    // untouched; pass a value to override it for this tick.
    bool setEmitter(VoiceHandle voice, const EmitterState& state, const WorldPosition& camera,
                    std::optional<float> dopplerFactor = std::nullopt) noexcept;
    bool setEmitterAttenuation(VoiceHandle voice, float minDistance, float maxDistance, float rolloff) noexcept;

    void setBusVolume(Bus bus, float volume) noexcept;
    [[nodiscard]] float busVolume(Bus bus) const noexcept;

private:
    struct Impl;
    explicit Audio(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

// Fixture generator for placeholder sounds: sine WAV bytes decodable by
// Audio::loadFromMemory, without shipping binary assets. Pure CPU.
[[nodiscard]] std::vector<std::uint8_t> generateToneWav(std::uint32_t sampleRate, std::uint32_t channels,
                                                         float frequencyHz, float durationSeconds,
                                                         float amplitude) noexcept;

} // namespace starforge
