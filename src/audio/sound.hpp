#pragma once

// Sound bank (M12.2): owns decoded ma_sound voices and per-bus
// ma_sound_group routing. Sounds are loaded from in-memory bytes
// (WAV/FLAC/MP3 via miniaudio's decoders); file loading and streaming
// arrive with the M17 asset pipeline. The bank borrows its
// AudioContext, which must outlive the bank: declare the bank AFTER
// the context so it is destroyed first (enforced via
// AudioContext's teardown assertion).

#include "audio_context.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct ma_decoder;
struct ma_sound;
using ma_sound_group = ma_sound;

namespace starforge::audio
{

// Same forward-declaration seam as audio_context.hpp: deleters are
// default-constructible (unique_ptr members default-init cleanly)
// and defined in sound.cpp where the C structs are complete.
struct MaSoundDeleter
{
    void operator()(ma_sound* sound) const noexcept;
};
struct MaDecoderDeleter
{
    void operator()(ma_decoder* decoder) const noexcept;
};
struct MaSoundGroupDeleter
{
    void operator()(ma_sound_group* group) const noexcept;
};
using MaSoundHandle = std::unique_ptr<ma_sound, MaSoundDeleter>;
using MaDecoderHandle = std::unique_ptr<ma_decoder, MaDecoderDeleter>;
using MaSoundGroupHandle = std::unique_ptr<ma_sound_group, MaSoundGroupDeleter>;



// Invalid handle: isValid() is false (index == kInvalidIndex).
struct SoundHandle
{
    static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFU;
    std::uint32_t index = kInvalidIndex;
    [[nodiscard]] bool isValid() const noexcept { return index != kInvalidIndex; }
};

struct SoundConfig
{
    Bus bus = Bus::SFX;
    bool loop = false;
    float volume = 1.0f;
    float pitch = 1.0f;
};

class SoundBank
{
public:
    // Fallible construction per the project convention: group init can
    // fail, so a bank is never a "zombie" with half-init groups.
    [[nodiscard]] static std::expected<SoundBank, std::string> create(AudioContext& context);

    SoundBank() noexcept;
    ~SoundBank();

    SoundBank(const SoundBank&) = delete;
    SoundBank& operator=(const SoundBank&) = delete;
    SoundBank(SoundBank&& other) noexcept;
    SoundBank& operator=(SoundBank&& other) noexcept;

    // Decodes in-memory audio data and creates a stopped voice.
    // The encoded bytes are copied into the Sound and retained for
    // the voice's lifetime (ma_decoder_init_memory does not copy).
    [[nodiscard]] std::expected<SoundHandle, std::string>
    loadFromMemory(std::span<const std::uint8_t> data, const SoundConfig& config = {});

    // Voice control. Invalid handles return false and have no effect.
    bool play(SoundHandle handle, bool restartIfPlaying = false) noexcept;
    bool playWithVolume(SoundHandle handle, float volume, bool restartIfPlaying = true) noexcept;
    bool stop(SoundHandle handle) noexcept;
    [[nodiscard]] bool isPlaying(SoundHandle handle) const noexcept;
    [[nodiscard]] bool isLooping(SoundHandle handle) const noexcept;
    [[nodiscard]] bool isAtEnd(SoundHandle handle) const noexcept;
    [[nodiscard]] bool isSpatializationEnabled(SoundHandle handle) const noexcept;
    bool setLoop(SoundHandle handle, bool loop) noexcept;
    bool setVolume(SoundHandle handle, float volume) noexcept;
    bool setPitch(SoundHandle handle, float pitch) noexcept;
    [[nodiscard]] float volume(SoundHandle handle) const noexcept;
    [[nodiscard]] float pitch(SoundHandle handle) const noexcept;

    // 3D emitter pose (camera-relative float32): sets position,
    // velocity, and the Doppler factor. 2D buses (UI/Music) reject
    // with false — they have spatialization disabled by design.
    bool setEmitterPose(SoundHandle handle, const math::Vec3f& position, const math::Vec3f& velocity,
                        float dopplerFactor = 1.0f) noexcept;
    bool setEmitterAttenuation(SoundHandle handle, float minDistance, float maxDistance, float rolloff) noexcept;
    bool setEmitterDopplerFactor(SoundHandle handle, float dopplerFactor) noexcept;
    [[nodiscard]] math::Vec3f emitterPosition(SoundHandle handle) const noexcept;
    [[nodiscard]] math::Vec3f emitterVelocity(SoundHandle handle) const noexcept;
    [[nodiscard]] float emitterDopplerFactor(SoundHandle handle) const noexcept;

    // Per-bus gain in [0, 1]; Master routes to the engine master volume.
    void setBusVolume(Bus bus, float volume) noexcept;
    [[nodiscard]] float busVolume(Bus bus) const noexcept;

    [[nodiscard]] std::size_t soundCount() const noexcept { return sounds_.size(); }

private:
    struct Sound
    {
        MaSoundHandle voice;
        // Retained encoded bytes: ma_decoder_init_memory keeps a raw
        // pointer and reads lazily, so the caller's buffer must never
        // be the lifetime source for playback.
        std::vector<std::uint8_t> encodedData;
        // Decoder feeding the voice; owned by the bank for the voice's
        // lifetime (ma_decoder_uninit runs after ma_sound_uninit).
        MaDecoderHandle decoder;
        SoundConfig config;
    };

    // Number of non-master buses (Music, SFX, UI, Voice).
    static constexpr std::size_t kGroupCount = 4U;

    [[nodiscard]] Sound* find(SoundHandle handle) noexcept;
    [[nodiscard]] const Sound* find(SoundHandle handle) const noexcept;
    void teardownGroups() noexcept;
    void teardownSounds() noexcept;

    AudioContext* context_ = nullptr;
    // Routable groups indexed by (Bus - Music): Music=0, SFX=1, UI=2, Voice=3.
    MaSoundGroupHandle groups_[kGroupCount]{};
    bool groupsInitialized_ = false;
    std::vector<std::unique_ptr<Sound>> sounds_;
    float busVolumes_[kGroupCount] = {1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace starforge::audio
