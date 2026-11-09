#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "world_bridge.hpp"

// Internal bridge between the public audio facade and the internal
// audio backend. Uses only plain data so no translation unit ever
// includes both the public math header and the internal GLM-based
// math aliases. RawAudio hides its internal context/bank behind an
// Impl because the internal audio headers require the internal math
// header. This header must not be installed.
namespace starforge::public_api_bridge
{

struct RawSoundParams
{
    std::uint8_t bus = 2; // SFX
    bool loop = false;
    float volume = 1.0f;
    float pitch = 1.0f;
    float rolloff = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 1000.0f;
    float dopplerFactor = 1.0f;
};

struct RawVoiceHandle
{
    std::uint32_t index = 0xFFFFFFFFU;
    [[nodiscard]] bool isValid() const noexcept { return index != 0xFFFFFFFFU; }
};

struct RawListenerState
{
    RawWorldPosition position;
    RawVec3d velocity;
    RawVec3f forward{0.0f, 0.0f, -1.0f};
    RawVec3f up{0.0f, 1.0f, 0.0f};
    RawWorldPosition camera;
};

struct RawEmitterState
{
    RawWorldPosition position;
    RawVec3d velocity;
    RawWorldPosition camera;
};

// Owns the internal AudioContext/SoundBank.
class RawAudio
{
public:
    struct CreateInfo
    {
        bool useSilentBackend = true;
        std::uint32_t sampleRate = 48000;
        std::uint32_t channels = 2;
    };

    [[nodiscard]] static std::expected<std::unique_ptr<RawAudio>, std::string> create(const CreateInfo& info);

    ~RawAudio();
    RawAudio(const RawAudio&) = delete;
    RawAudio& operator=(const RawAudio&) = delete;
    RawAudio(RawAudio&& other) noexcept;
    RawAudio& operator=(RawAudio&& other) noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept;

    void setMasterVolume(float volume) noexcept;
    [[nodiscard]] float masterVolume() const noexcept;

    [[nodiscard]] std::expected<RawVoiceHandle, std::string>
    loadFromMemory(std::span<const std::uint8_t> data, const RawSoundParams& params = {});

    bool play(RawVoiceHandle voice, bool restartIfPlaying = false) noexcept;
    bool stop(RawVoiceHandle voice) noexcept;
    [[nodiscard]] bool isPlaying(RawVoiceHandle voice) const noexcept;
    bool setVolume(RawVoiceHandle voice, float volume) noexcept;
    bool setPitch(RawVoiceHandle voice, float pitch) noexcept;
    bool setLoop(RawVoiceHandle voice, bool loop) noexcept;
    [[nodiscard]] float volume(RawVoiceHandle voice) const noexcept;
    [[nodiscard]] float pitch(RawVoiceHandle voice) const noexcept;
    [[nodiscard]] bool isLooping(RawVoiceHandle voice) const noexcept;

    void setListener(const RawListenerState& state) noexcept;
    bool setEmitter(RawVoiceHandle voice, const RawEmitterState& state,
                    std::optional<float> dopplerFactor = std::nullopt) noexcept;
    bool setEmitterAttenuation(RawVoiceHandle voice, float minDistance, float maxDistance, float rolloff) noexcept;

    void setBusVolume(std::uint8_t bus, float volume) noexcept;
    [[nodiscard]] float busVolume(std::uint8_t bus) const noexcept;

private:
    RawAudio() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
