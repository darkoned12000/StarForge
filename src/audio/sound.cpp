#include "sound.hpp"

#include "core/log.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <format>
#include <utility>

namespace starforge::audio
{
namespace
{

constexpr std::string_view kTag = "audio";

[[nodiscard]] std::size_t groupIndex(Bus bus) noexcept
{
    // Master has no group (engine gain); caller guards against it.
    return static_cast<std::size_t>(bus) - 1U;
}

[[nodiscard]] float clampGain(float gain) noexcept
{
    return std::clamp(gain, 0.0f, 1.0f);
}

// Music and UI are 2D buses: spatialization would pan/attenuate them
// with camera movement, so they are explicitly non-spatial (M12.3
// listener motion must not affect them). SFX/Voice stay spatial.
[[nodiscard]] bool isTwoDimensional(Bus bus) noexcept
{
    return bus == Bus::UI || bus == Bus::Music;
}

} // namespace

SoundBank::SoundBank() noexcept = default;

std::expected<SoundBank, std::string> SoundBank::create(AudioContext& context)
{
    if (!context.isValid())
    {
        return std::unexpected<std::string>("sound bank requires a valid audio context");
    }

    SoundBank bank;
    bank.context_ = &context;
    for (std::size_t i = 0; i < kGroupCount; ++i)
    {
        bank.groups_[i] = new ma_sound_group;
        const ma_result result = ma_sound_group_init(context.engineHandle(), 0, nullptr, bank.groups_[i]);
        if (result != MA_SUCCESS)
        {
            core::logWarning(kTag, "SoundBank: failed to init bus group {}", i);
            bank.teardownGroups();
            return std::unexpected<std::string>(
                std::format("failed to init bus group {}: {}", i, static_cast<int>(result)));
        }
    }
    bank.groupsInitialized_ = true;
    context.registerBank();
    return bank;
}

SoundBank::~SoundBank()
{
    teardownSounds();
    teardownGroups();
    if (context_ != nullptr)
    {
        context_->unregisterBank();
        context_ = nullptr;
    }
}

SoundBank::SoundBank(SoundBank&& other) noexcept
    : context_(other.context_), groupsInitialized_(other.groupsInitialized_),
      sounds_(std::move(other.sounds_))
{
    for (std::size_t i = 0; i < kGroupCount; ++i)
    {
        groups_[i] = other.groups_[i];
        other.groups_[i] = nullptr;
        busVolumes_[i] = other.busVolumes_[i];
    }
    other.context_ = nullptr;
    other.groupsInitialized_ = false;
}

SoundBank& SoundBank::operator=(SoundBank&& other) noexcept
{
    if (this != &other)
    {
        teardownSounds();
        teardownGroups();
        if (context_ != nullptr)
        {
            context_->unregisterBank();
            context_ = nullptr;
        }
        context_ = other.context_;
        groupsInitialized_ = other.groupsInitialized_;
        sounds_ = std::move(other.sounds_);
        for (std::size_t i = 0; i < kGroupCount; ++i)
        {
            groups_[i] = other.groups_[i];
        other.groups_[i] = nullptr;
            busVolumes_[i] = other.busVolumes_[i];
        }
        other.context_ = nullptr;
        other.groupsInitialized_ = false;
    }
    return *this;
}

void SoundBank::teardownSounds() noexcept
{
    // Sounds must uninit before their groups (sounds reference groups),
    // and each voice before its decoder.
    for (auto& sound : sounds_)
    {
        if (sound->voice != nullptr)
        {
            ma_sound_uninit(sound->voice.get());
            sound->voice.reset();
        }
        if (sound->decoder != nullptr)
        {
            ma_decoder_uninit(sound->decoder.get());
            sound->decoder.reset();
        }
    }
    sounds_.clear();
}

void SoundBank::teardownGroups() noexcept
{
    if (!groupsInitialized_ || context_ == nullptr)
    {
        return;
    }
    for (std::size_t i = 0; i < kGroupCount; ++i)
    {
        if (groups_[i] != nullptr)
        {
            (void)ma_sound_group_uninit(groups_[i]);
            delete groups_[i];
            groups_[i] = nullptr;
        }
    }
    groupsInitialized_ = false;
}

SoundBank::Sound* SoundBank::find(SoundHandle handle) noexcept
{
    if (!handle.isValid() || handle.index >= sounds_.size())
    {
        return nullptr;
    }
    return sounds_[handle.index].get();
}

const SoundBank::Sound* SoundBank::find(SoundHandle handle) const noexcept
{
    if (!handle.isValid() || handle.index >= sounds_.size())
    {
        return nullptr;
    }
    return sounds_[handle.index].get();
}

std::expected<SoundHandle, std::string>
SoundBank::loadFromMemory(std::span<const std::uint8_t> data, const SoundConfig& config)
{
    if (context_ == nullptr || !context_->isValid() || !groupsInitialized_)
    {
        return std::unexpected<std::string>("sound bank has no valid audio context");
    }
    if (data.empty())
    {
        return std::unexpected<std::string>("no audio data to decode");
    }

    auto sound = std::make_unique<Sound>();
    sound->config = config;
    // Retain the encoded bytes: ma_decoder_init_memory stores a raw
    // pointer and reads lazily during playback, so the caller's
    // buffer must not be the lifetime source (UAF fix, M12.2 review).
    // Copied BEFORE decoder init and never modified afterwards, so
    // the decoder's pointer stays valid for the Sound's lifetime.
    sound->encodedData.assign(data.begin(), data.end());
    sound->decoder = std::make_unique<ma_decoder>();
    sound->voice = std::make_unique<ma_sound>();

    const ma_result decodeResult =
        ma_decoder_init_memory(sound->encodedData.data(), sound->encodedData.size(), nullptr, sound->decoder.get());
    if (decodeResult != MA_SUCCESS)
    {
        return std::unexpected<std::string>(
            std::format("failed to decode audio data: {}", static_cast<int>(decodeResult)));
    }

    ma_sound_group* group = nullptr;
    if (config.bus != Bus::Master)
    {
        group = groups_[groupIndex(config.bus)];
    }
    // MA_SOUND_FLAG_DECODE is a resource-manager-only flag and is
    // ignored for data-source sounds: playback streams via the
    // decoder (which now reads from retained memory).
    ma_uint32 flags = 0;
    if (config.loop)
    {
        flags |= MA_SOUND_FLAG_LOOPING;
    }
    // ma_decoder implements the ma_data_source interface (data-source
    // base is its first member); route through the interface type.
    const ma_result result = ma_sound_init_from_data_source(
        context_->engineHandle(), static_cast<ma_data_source*>(static_cast<void*>(sound->decoder.get())), flags,
        group, sound->voice.get());
    if (result != MA_SUCCESS)
    {
        ma_decoder_uninit(sound->decoder.get());
        return std::unexpected<std::string>(
            std::format("failed to create sound from audio data: {}", static_cast<int>(result)));
    }

    // 2D buses (UI/Music) must not be spatialized by listener motion.
    if (isTwoDimensional(config.bus))
    {
        ma_sound_set_spatialization_enabled(sound->voice.get(), MA_FALSE);
    }

    ma_sound_set_volume(sound->voice.get(), clampGain(config.volume));
    ma_sound_set_pitch(sound->voice.get(), config.pitch > 0.0f ? config.pitch : 1.0f);

    const auto handle = SoundHandle{static_cast<std::uint32_t>(sounds_.size())};
    sounds_.push_back(std::move(sound));
    core::logDebug(kTag, "SoundBank: loaded sound #{} ({} bytes, bus {})", handle.index, data.size(),
                   static_cast<int>(config.bus));
    return handle;
}

bool SoundBank::play(SoundHandle handle) noexcept
{
    Sound* sound = find(handle);
    if (sound == nullptr || sound->voice == nullptr)
    {
        return false;
    }
    // A finished one-shot sits at EOF: ma_sound_start() alone would
    // silently "succeed" without producing sound. Rewind first so
    // one-shots can be replayed.
    if (ma_sound_at_end(sound->voice.get()))
    {
        (void)ma_sound_seek_to_pcm_frame(sound->voice.get(), 0);
    }
    return ma_sound_start(sound->voice.get()) == MA_SUCCESS;
}

bool SoundBank::stop(SoundHandle handle) noexcept
{
    Sound* sound = find(handle);
    if (sound == nullptr || sound->voice == nullptr)
    {
        return false;
    }
    return ma_sound_stop(sound->voice.get()) == MA_SUCCESS;
}

bool SoundBank::isPlaying(SoundHandle handle) const noexcept
{
    const Sound* sound = find(handle);
    return sound != nullptr && sound->voice != nullptr && ma_sound_is_playing(sound->voice.get()) == MA_TRUE;
}

bool SoundBank::isLooping(SoundHandle handle) const noexcept
{
    const Sound* sound = find(handle);
    return sound != nullptr && sound->voice != nullptr && ma_sound_is_looping(sound->voice.get()) == MA_TRUE;
}

bool SoundBank::isAtEnd(SoundHandle handle) const noexcept
{
    const Sound* sound = find(handle);
    return sound != nullptr && sound->voice != nullptr && ma_sound_at_end(sound->voice.get()) == MA_TRUE;
}

bool SoundBank::isSpatializationEnabled(SoundHandle handle) const noexcept
{
    const Sound* sound = find(handle);
    return sound != nullptr && sound->voice != nullptr &&
           ma_sound_is_spatialization_enabled(sound->voice.get()) == MA_TRUE;
}

bool SoundBank::setLoop(SoundHandle handle, bool loop) noexcept
{
    Sound* sound = find(handle);
    if (sound == nullptr || sound->voice == nullptr)
    {
        return false;
    }
    sound->config.loop = loop;
    ma_sound_set_looping(sound->voice.get(), loop ? MA_TRUE : MA_FALSE);
    return true;
}

bool SoundBank::setVolume(SoundHandle handle, float volume) noexcept
{
    Sound* sound = find(handle);
    if (sound == nullptr || sound->voice == nullptr)
    {
        return false;
    }
    sound->config.volume = clampGain(volume);
    ma_sound_set_volume(sound->voice.get(), sound->config.volume);
    return true;
}

bool SoundBank::setPitch(SoundHandle handle, float pitch) noexcept
{
    Sound* sound = find(handle);
    if (sound == nullptr || sound->voice == nullptr || pitch <= 0.0f)
    {
        return false;
    }
    sound->config.pitch = pitch;
    ma_sound_set_pitch(sound->voice.get(), pitch);
    return true;
}

float SoundBank::volume(SoundHandle handle) const noexcept
{
    const Sound* sound = find(handle);
    return sound != nullptr ? sound->config.volume : 0.0f;
}

float SoundBank::pitch(SoundHandle handle) const noexcept
{
    const Sound* sound = find(handle);
    return sound != nullptr ? sound->config.pitch : 0.0f;
}

void SoundBank::setBusVolume(Bus bus, float volume) noexcept
{
    const float clamped = clampGain(volume);
    if (bus == Bus::Master)
    {
        if (context_ != nullptr)
        {
            context_->setMasterVolume(clamped);
        }
        return;
    }
    const std::size_t index = groupIndex(bus);
    busVolumes_[index] = clamped;
    if (groupsInitialized_ && context_ != nullptr && groups_[index] != nullptr)
    {
        ma_sound_group_set_volume(groups_[index], clamped);
    }
}

float SoundBank::busVolume(Bus bus) const noexcept
{
    if (bus == Bus::Master)
    {
        return context_ != nullptr ? context_->masterVolume() : 0.0f;
    }
    return busVolumes_[groupIndex(bus)];
}

} // namespace starforge::audio
