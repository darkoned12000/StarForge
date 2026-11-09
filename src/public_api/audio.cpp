#include "starforge/audio.hpp"

#include "audio/tone_generator.hpp"
#include "audio_bridge.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace starforge
{
namespace
{

namespace bridge = public_api_bridge;

[[nodiscard]] bridge::RawWorldPosition toRaw(const WorldPosition& value) noexcept
{
    return bridge::RawWorldPosition{value.cellX, value.cellY, value.cellZ, value.offsetX, value.offsetY, value.offsetZ};
}

[[nodiscard]] bridge::RawVec3d toRaw(const math::Vec3d& value) noexcept
{
    return bridge::RawVec3d{value.x(), value.y(), value.z()};
}

[[nodiscard]] bridge::RawVec3f toRaw(const math::Vec3f& value) noexcept
{
    return bridge::RawVec3f{value.x(), value.y(), value.z()};
}

[[nodiscard]] std::uint8_t toRaw(Audio::Bus bus) noexcept
{
    switch (bus)
    {
        case Audio::Bus::Master:
            return 0U;
        case Audio::Bus::Music:
            return 1U;
        case Audio::Bus::UI:
            return 3U;
        case Audio::Bus::Voice:
            return 4U;
        default:
            return 2U;
    }
}

bridge::RawSoundParams toRaw(const Audio::SoundParams& params) noexcept
{
    bridge::RawSoundParams result;
    result.bus = toRaw(params.bus);
    result.loop = params.loop;
    result.volume = params.volume;
    result.pitch = params.pitch;
    result.rolloff = params.rolloff;
    result.minDistance = params.minDistance;
    result.maxDistance = params.maxDistance;
    result.dopplerFactor = 1.0f;
    return result;
}

} // namespace

struct Audio::Impl
{
    std::unique_ptr<bridge::RawAudio> backend;
};

Audio::Audio(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

Audio::~Audio() = default;

Audio::Audio(Audio&& other) noexcept : impl_(std::move(other.impl_))
{
}

Audio& Audio::operator=(Audio&& other) noexcept
{
    impl_ = std::move(other.impl_);
    return *this;
}

std::expected<Audio, std::string> Audio::create(const CreateInfo& info)
{
    bridge::RawAudio::CreateInfo backendInfo;
    backendInfo.useSilentBackend = info.backend == CreateInfo::Backend::Silent;
    backendInfo.sampleRate = info.sampleRate;
    backendInfo.channels = info.channels;
    auto backend = bridge::RawAudio::create(backendInfo);
    if (!backend)
    {
        return std::unexpected<std::string>(backend.error());
    }
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    return Audio(std::move(impl));
}

bool Audio::isRunning() const noexcept
{
    return impl_ != nullptr && impl_->backend->isRunning();
}

std::uint32_t Audio::sampleRate() const noexcept
{
    return impl_ != nullptr ? impl_->backend->sampleRate() : 0U;
}

std::uint32_t Audio::channels() const noexcept
{
    return impl_ != nullptr ? impl_->backend->channels() : 0U;
}

void Audio::setMasterVolume(float volume) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->backend->setMasterVolume(volume);
    }
}

float Audio::masterVolume() const noexcept
{
    return impl_ != nullptr ? impl_->backend->masterVolume() : 0.0f;
}

std::expected<Audio::VoiceHandle, std::string> Audio::loadFromMemory(std::span<const std::uint8_t> data)
{
    return loadFromMemory(data, SoundParams{});
}

std::expected<Audio::VoiceHandle, std::string> Audio::loadFromMemory(std::span<const std::uint8_t> data, const SoundParams& params)
{
    if (impl_ == nullptr)
    {
        return std::unexpected<std::string>("Audio is moved-from");
    }
    auto handle = impl_->backend->loadFromMemory(data, toRaw(params));
    if (!handle)
    {
        return std::unexpected<std::string>(handle.error());
    }
    return VoiceHandle{(*handle).index};
}

bool Audio::play(VoiceHandle voice, bool restartIfPlaying) noexcept
{
    return impl_ != nullptr && impl_->backend->play(bridge::RawVoiceHandle{voice.index}, restartIfPlaying);
}

bool Audio::stop(VoiceHandle voice) noexcept
{
    return impl_ != nullptr && impl_->backend->stop(bridge::RawVoiceHandle{voice.index});
}

bool Audio::isPlaying(VoiceHandle voice) const noexcept
{
    return impl_ != nullptr && impl_->backend->isPlaying(bridge::RawVoiceHandle{voice.index});
}

bool Audio::setVolume(VoiceHandle voice, float volume) noexcept
{
    return impl_ != nullptr && impl_->backend->setVolume(bridge::RawVoiceHandle{voice.index}, volume);
}

bool Audio::setPitch(VoiceHandle voice, float pitch) noexcept
{
    return impl_ != nullptr && impl_->backend->setPitch(bridge::RawVoiceHandle{voice.index}, pitch);
}

bool Audio::setLoop(VoiceHandle voice, bool loop) noexcept
{
    return impl_ != nullptr && impl_->backend->setLoop(bridge::RawVoiceHandle{voice.index}, loop);
}

float Audio::volume(VoiceHandle voice) const noexcept
{
    return impl_ != nullptr ? impl_->backend->volume(bridge::RawVoiceHandle{voice.index}) : 0.0f;
}

float Audio::pitch(VoiceHandle voice) const noexcept
{
    return impl_ != nullptr ? impl_->backend->pitch(bridge::RawVoiceHandle{voice.index}) : 0.0f;
}

bool Audio::isLooping(VoiceHandle voice) const noexcept
{
    return impl_ != nullptr && impl_->backend->isLooping(bridge::RawVoiceHandle{voice.index});
}

void Audio::setListener(const ListenerState& state, const WorldPosition& camera) noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }
    bridge::RawListenerState raw;
    raw.position = toRaw(state.position);
    raw.velocity = toRaw(state.velocity);
    raw.forward = toRaw(state.forward);
    raw.up = toRaw(state.up);
    raw.camera = toRaw(camera);
    impl_->backend->setListener(raw);
}

bool Audio::setEmitter(VoiceHandle voice, const EmitterState& state, const WorldPosition& camera,
                       std::optional<float> dopplerFactor) noexcept
{
    if (impl_ == nullptr)
    {
        return false;
    }
    bridge::RawEmitterState raw;
    raw.position = toRaw(state.position);
    raw.velocity = toRaw(state.velocity);
    raw.camera = toRaw(camera);
    return impl_->backend->setEmitter(bridge::RawVoiceHandle{voice.index}, raw, dopplerFactor);
}

bool Audio::setEmitterAttenuation(VoiceHandle voice, float minDistance, float maxDistance, float rolloff) noexcept
{
    return impl_ != nullptr &&
           impl_->backend->setEmitterAttenuation(bridge::RawVoiceHandle{voice.index}, minDistance, maxDistance, rolloff);
}

void Audio::setBusVolume(Bus bus, float volume) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->backend->setBusVolume(toRaw(bus), volume);
    }
}

float Audio::busVolume(Bus bus) const noexcept
{
    return impl_ != nullptr ? impl_->backend->busVolume(toRaw(bus)) : 0.0f;
}

std::vector<std::uint8_t> generateToneWav(std::uint32_t sampleRate, std::uint32_t channels,
                                          float frequencyHz, float durationSeconds, float amplitude) noexcept
{
    audio::ToneParams params;
    params.sampleRate = sampleRate;
    params.channels = channels;
    params.frequencyHz = frequencyHz;
    params.durationSeconds = durationSeconds;
    params.amplitude = amplitude;
    return audio::generateToneWav(params);
}

} // namespace starforge
