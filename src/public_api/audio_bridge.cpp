#include "audio_bridge.hpp"

#include "audio/audio_context.hpp"
#include "audio/sound.hpp"
#include "audio/spatial.hpp"

#include <cmath>

namespace starforge::public_api_bridge
{
namespace
{

using namespace starforge::audio;

[[nodiscard]] spatial::WorldPosition toSpatial(const RawWorldPosition& raw) noexcept
{
    spatial::WorldPosition result;
    result.cellX = raw.cellX;
    result.cellY = raw.cellY;
    result.cellZ = raw.cellZ;
    result.offsetX = raw.offsetX;
    result.offsetY = raw.offsetY;
    result.offsetZ = raw.offsetZ;
    return result;
}

[[nodiscard]] Bus fromRaw(std::uint8_t bus) noexcept
{
    switch (bus)
    {
        case 1:
            return Bus::Music;
        case 3:
            return Bus::UI;
        case 4:
            return Bus::Voice;
        case 0:
            return Bus::Master;
        default:
            return Bus::SFX;
    }
}

[[nodiscard]] starforge::math::Vec3f toInternalVec3f(const RawVec3f& raw) noexcept
{
    return starforge::math::Vec3f{raw.x, raw.y, raw.z};
}

[[nodiscard]] starforge::math::Vec3f normalizedOr(const starforge::math::Vec3f& value,
                                                  const starforge::math::Vec3f& fallback) noexcept
{
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length < 0.0001f || !std::isfinite(length))
    {
        return fallback;
    }
    return value / length;
}

} // namespace

struct RawAudio::Impl
{
    audio::AudioContext context;
    audio::SoundBank bank;
};

RawAudio::~RawAudio() = default;

RawAudio::RawAudio(RawAudio&& other) noexcept : impl_(std::move(other.impl_))
{
}

RawAudio& RawAudio::operator=(RawAudio&& other) noexcept
{
    impl_ = std::move(other.impl_);
    return *this;
}

std::expected<std::unique_ptr<RawAudio>, std::string> RawAudio::create(const CreateInfo& info)
{
    audio::AudioContext::Config config;
    config.sampleRate = info.sampleRate;
    config.channels = info.channels;
    config.backend = info.useSilentBackend ? audio::AudioContext::Config::Backend::Null
                                           : audio::AudioContext::Config::Backend::Default;
    auto context = audio::AudioContext::create(config);
    if (!context)
    {
        return std::unexpected<std::string>(context.error());
    }

    auto audio = std::unique_ptr<RawAudio>(new RawAudio());
    audio->impl_ = std::make_unique<Impl>();
    // Move the context into the bank's final owner before registering
    // the bank, or the bank's context pointer would dangle.
    audio->impl_->context = std::move(*context);

    auto bank = audio::SoundBank::create(audio->impl_->context);
    if (!bank)
    {
        return std::unexpected<std::string>(bank.error());
    }
    audio->impl_->bank = std::move(*bank);
    return audio;
}

bool RawAudio::isRunning() const noexcept
{
    return impl_ != nullptr && impl_->context.isRunning();
}

std::uint32_t RawAudio::sampleRate() const noexcept
{
    return impl_ != nullptr ? impl_->context.sampleRate() : 0U;
}

std::uint32_t RawAudio::channels() const noexcept
{
    return impl_ != nullptr ? impl_->context.channels() : 0U;
}

void RawAudio::setMasterVolume(float volume) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->context.setMasterVolume(volume);
    }
}

float RawAudio::masterVolume() const noexcept
{
    return impl_ != nullptr ? impl_->context.masterVolume() : 0.0f;
}

std::expected<RawVoiceHandle, std::string> RawAudio::loadFromMemory(std::span<const std::uint8_t> data, const RawSoundParams& params)
{
    if (impl_ == nullptr)
    {
        return std::unexpected<std::string>("RawAudio is moved-from");
    }
    audio::SoundConfig config;
    config.bus = fromRaw(params.bus);
    config.loop = params.loop;
    config.volume = params.volume;
    config.pitch = params.pitch;
    auto handle = impl_->bank.loadFromMemory(data, config);
    if (!handle)
    {
        return std::unexpected<std::string>(handle.error());
    }
    (void)impl_->bank.setEmitterAttenuation(*handle, params.minDistance, params.maxDistance, params.rolloff);
    (void)impl_->bank.setEmitterDopplerFactor(*handle, params.dopplerFactor);
    return RawVoiceHandle{(*handle).index};
}

bool RawAudio::play(RawVoiceHandle voice, bool restartIfPlaying) noexcept
{
    return impl_ != nullptr && impl_->bank.play(audio::SoundHandle{voice.index}, restartIfPlaying);
}

bool RawAudio::stop(RawVoiceHandle voice) noexcept
{
    return impl_ != nullptr && impl_->bank.stop(audio::SoundHandle{voice.index});
}

bool RawAudio::isPlaying(RawVoiceHandle voice) const noexcept
{
    return impl_ != nullptr && impl_->bank.isPlaying(audio::SoundHandle{voice.index});
}

bool RawAudio::setVolume(RawVoiceHandle voice, float volume) noexcept
{
    return impl_ != nullptr && impl_->bank.setVolume(audio::SoundHandle{voice.index}, volume);
}

bool RawAudio::setPitch(RawVoiceHandle voice, float pitch) noexcept
{
    return impl_ != nullptr && impl_->bank.setPitch(audio::SoundHandle{voice.index}, pitch);
}

bool RawAudio::setLoop(RawVoiceHandle voice, bool loop) noexcept
{
    return impl_ != nullptr && impl_->bank.setLoop(audio::SoundHandle{voice.index}, loop);
}

float RawAudio::volume(RawVoiceHandle voice) const noexcept
{
    return impl_ != nullptr ? impl_->bank.volume(audio::SoundHandle{voice.index}) : 0.0f;
}

float RawAudio::pitch(RawVoiceHandle voice) const noexcept
{
    return impl_ != nullptr ? impl_->bank.pitch(audio::SoundHandle{voice.index}) : 0.0f;
}

bool RawAudio::isLooping(RawVoiceHandle voice) const noexcept
{
    return impl_ != nullptr && impl_->bank.isLooping(audio::SoundHandle{voice.index});
}

void RawAudio::setListener(const RawListenerState& state) noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }
    const auto position = toCameraRelativeFloat(toSpatial(state.position), toSpatial(state.camera));
    const auto velocity = starforge::math::Vec3f{static_cast<float>(state.velocity.x),
                                                 static_cast<float>(state.velocity.y),
                                                 static_cast<float>(state.velocity.z)};
    const auto forward = normalizedOr(toInternalVec3f(state.forward), starforge::math::Vec3f{0.0f, 0.0f, -1.0f});
    auto up = normalizedOr(toInternalVec3f(state.up), starforge::math::Vec3f{0.0f, 1.0f, 0.0f});
    const float crossX = forward.y * up.z - forward.z * up.y;
    const float crossY = forward.z * up.x - forward.x * up.z;
    const float crossZ = forward.x * up.y - forward.y * up.x;
    const float crossLength = std::sqrt(crossX * crossX + crossY * crossY + crossZ * crossZ);
    if (crossLength < 0.0001f || !std::isfinite(crossLength))
    {
        up = std::abs(forward.y) > 0.99f ? starforge::math::Vec3f{1.0f, 0.0f, 0.0f}
                                         : starforge::math::Vec3f{0.0f, 1.0f, 0.0f};
    }
    (void)impl_->context.setListenerPose(position, velocity, forward, up);
}

bool RawAudio::setEmitter(RawVoiceHandle voice, const RawEmitterState& state,
                          std::optional<float> dopplerFactor) noexcept
{
    if (impl_ == nullptr)
    {
        return false;
    }
    const auto position = toCameraRelativeFloat(toSpatial(state.position), toSpatial(state.camera));
    const auto velocity = starforge::math::Vec3f{static_cast<float>(state.velocity.x),
                                                 static_cast<float>(state.velocity.y),
                                                 static_cast<float>(state.velocity.z)};
    return impl_->bank.setEmitterPose(audio::SoundHandle{voice.index}, position, velocity, dopplerFactor);
}

bool RawAudio::setEmitterAttenuation(RawVoiceHandle voice, float minDistance, float maxDistance, float rolloff) noexcept
{
    return impl_ != nullptr &&
           impl_->bank.setEmitterAttenuation(audio::SoundHandle{voice.index}, minDistance, maxDistance, rolloff);
}

void RawAudio::setBusVolume(std::uint8_t bus, float volume) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->bank.setBusVolume(fromRaw(bus), volume);
    }
}

float RawAudio::busVolume(std::uint8_t bus) const noexcept
{
    return impl_ != nullptr ? impl_->bank.busVolume(fromRaw(bus)) : 0.0f;
}

} // namespace starforge::public_api_bridge
