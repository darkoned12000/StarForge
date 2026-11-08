#include "audio_context.hpp"

#include "core/log.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cassert>
#include <format>
#include <utility>

namespace starforge::audio
{
namespace
{

constexpr std::string_view kTag = "audio";

[[nodiscard]] bool isValidConfig(const AudioContext::Config& config, std::string& error)
{
    if (config.sampleRate < 4000 || config.sampleRate > 192000)
    {
        error = std::format("invalid sample rate: {}", config.sampleRate);
        return false;
    }
    if (config.channels == 0 || config.channels > 8)
    {
        error = std::format("invalid channel count: {}", config.channels);
        return false;
    }
    return true;
}

} // namespace

void MaContextDeleter::operator()(ma_context* context) const noexcept
{
    delete context;
}

void MaEngineDeleter::operator()(ma_engine* engine) const noexcept
{
    delete engine;
}

AudioContext::AudioContext() noexcept
{
}

AudioContext::~AudioContext()
{
    teardown();
}

AudioContext::AudioContext(AudioContext&& other) noexcept
    : context_(std::move(other.context_)), engine_(std::move(other.engine_)),
      initialized_(other.initialized_), masterVolume_(other.masterVolume_), backend_(other.backend_),
      liveBanks_(other.liveBanks_)
{
    // Banks hold a raw pointer to this context object: moving while
    // banks borrow it would strand them on the moved-from address.
    assert(other.liveBanks_ == 0 && "cannot move AudioContext while SoundBanks borrow it");
    other.initialized_ = false;
    other.liveBanks_ = 0;
}

AudioContext& AudioContext::operator=(AudioContext&& other) noexcept
{
    if (this != &other)
    {
        assert(other.liveBanks_ == 0 && "cannot move AudioContext while SoundBanks borrow it");
        teardown();
        context_ = std::move(other.context_);
        engine_ = std::move(other.engine_);
        initialized_ = other.initialized_;
        masterVolume_ = other.masterVolume_;
        backend_ = other.backend_;
        liveBanks_ = other.liveBanks_;
        other.initialized_ = false;
        other.liveBanks_ = 0;
    }
    return *this;
}

void AudioContext::teardown() noexcept
{
    // Partial init / moved-from: the unique_ptrs free the raw structs
    // regardless of initialized_, so failed creates never leak. Only a
    // live context reaches the uninit calls.
    if (!initialized_)
    {
        context_.reset();
        engine_.reset();
        return;
    }
    // Banks own voices/groups bound to this engine: destroying the
    // engine first would crash them. The ordering contract (bank
    // declared after context, destroyed before it) is asserted.
    if (liveBanks_ != 0)
    {
        core::logWarning(kTag, "AudioContext destroyed with {} live SoundBank(s) — bank must outlive "
                               "its context check: declare the bank after the context",
                         liveBanks_);
        assert(liveBanks_ == 0 && "AudioContext destroyed while a SoundBank is still alive");
    }
    // Teardown order: engine (owns its device) before context, matching
    // miniaudio's required uninit order.
    if (engine_ != nullptr)
    {
        (void)ma_engine_uninit(engine_.get());
    }
    if (context_ != nullptr)
    {
        (void)ma_context_uninit(context_.get());
    }
    engine_.reset();
    context_.reset();
    initialized_ = false;
}

void AudioContext::registerBank() noexcept
{
    ++liveBanks_;
}

void AudioContext::unregisterBank() noexcept
{
    if (liveBanks_ > 0)
    {
        --liveBanks_;
    }
}

std::expected<AudioContext, std::string> AudioContext::create(const Config& config)
{
    std::string error;
    if (!isValidConfig(config, error))
    {
        return std::unexpected<std::string>(error);
    }

    AudioContext result;
    result.backend_ = config.backend;
    result.context_ = MaContextHandle(new ma_context);
    result.engine_ = MaEngineHandle(new ma_engine);

    ma_context_config contextConfig = ma_context_config_init();

    ma_result contextResult = MA_SUCCESS;
    if (config.backend == Config::Backend::Null)
    {
        // Force the null backend: no audio hardware required. This is
        // the headless-CI path; the sandbox uses Default.
        const ma_backend nullBackend = ma_backend_null;
        contextResult = ma_context_init(&nullBackend, 1, &contextConfig, result.context_.get());
    }
    else
    {
        contextResult = ma_context_init(nullptr, 0, &contextConfig, result.context_.get());
    }
    if (contextResult != MA_SUCCESS)
    {
        return std::unexpected<std::string>(
            std::format("ma_context_init failed: {}", static_cast<int>(contextResult)));
    }

    // The engine creates and manages its own playback device internally.
    // Passing our pre-initialized context selects the backend (null vs
    // OS); sampleRate/channels request the caller's format (negotiation
    // may still adjust on real hardware).
    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.pContext = result.context_.get();
    engineConfig.sampleRate = config.sampleRate;
    engineConfig.channels = config.channels;

    const ma_result engineResult = ma_engine_init(&engineConfig, result.engine_.get());
    if (engineResult != MA_SUCCESS)
    {
        (void)ma_context_uninit(result.context_.get());
        // Let the result object free both structs on destruction.
        result.context_.reset();
        result.engine_.reset();
        return std::unexpected<std::string>(
            std::format("ma_engine_init failed: {}", static_cast<int>(engineResult)));
    }

    result.initialized_ = true;
    core::logDebug(kTag, "AudioContext created ({} backend)", result.backendName());
    return result;
}

bool AudioContext::isRunning() const noexcept
{
    return initialized_ && engine_ != nullptr && engine_->pDevice != nullptr &&
           ma_device_get_state(engine_->pDevice) == ma_device_state_started;
}

std::uint32_t AudioContext::sampleRate() const noexcept
{
    return (initialized_ && engine_ != nullptr && engine_->pDevice != nullptr) ? engine_->pDevice->sampleRate : 0U;
}

std::uint32_t AudioContext::channels() const noexcept
{
    return (initialized_ && engine_ != nullptr && engine_->pDevice != nullptr)
               ? engine_->pDevice->playback.channels
               : 0U;
}

const char* AudioContext::backendName() const noexcept
{
    return backend_ == Config::Backend::Null ? "null" : "default";
}

void AudioContext::setMasterVolume(float volume) noexcept
{
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (initialized_ && engine_ != nullptr)
    {
        (void)ma_engine_set_volume(engine_.get(), masterVolume_);
    }
}

bool AudioContext::setListenerPose(const math::Vec3f& position, const math::Vec3f& velocity,
                                   const math::Vec3f& forward, const math::Vec3f& up) noexcept
{
    if (initialized_ && engine_ != nullptr)
    {
        ma_engine_listener_set_position(engine_.get(), 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(engine_.get(), 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_velocity(engine_.get(), 0, velocity.x, velocity.y, velocity.z);
        ma_engine_listener_set_world_up(engine_.get(), 0, up.x, up.y, up.z);
        return true;
    }
    return false;
}

math::Vec3f AudioContext::listenerPosition() const noexcept
{
    if (initialized_ && engine_ != nullptr)
    {
        const ma_vec3f pos = ma_engine_listener_get_position(engine_.get(), 0);
        return {pos.x, pos.y, pos.z};
    }
    return {};
}

math::Vec3f AudioContext::listenerVelocity() const noexcept
{
    if (initialized_ && engine_ != nullptr)
    {
        const ma_vec3f vel = ma_engine_listener_get_velocity(engine_.get(), 0);
        return {vel.x, vel.y, vel.z};
    }
    return {};
}

math::Vec3f AudioContext::listenerDirection() const noexcept
{
    if (initialized_ && engine_ != nullptr)
    {
        const ma_vec3f dir = ma_engine_listener_get_direction(engine_.get(), 0);
        return {dir.x, dir.y, dir.z};
    }
    return {};
}

} // namespace starforge::audio
