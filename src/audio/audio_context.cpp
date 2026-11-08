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

AudioContext::AudioContext() noexcept = default;

AudioContext::~AudioContext()
{
    teardown();
}

AudioContext::AudioContext(AudioContext&& other) noexcept
    : context_(other.context_), engine_(other.engine_), initialized_(other.initialized_),
      masterVolume_(other.masterVolume_), backend_(other.backend_), liveBanks_(other.liveBanks_)
{
    other.context_ = nullptr;
    other.engine_ = nullptr;
    other.initialized_ = false;
    other.liveBanks_ = 0;
}

AudioContext& AudioContext::operator=(AudioContext&& other) noexcept
{
    if (this != &other)
    {
        teardown();
        context_ = other.context_;
        engine_ = other.engine_;
        initialized_ = other.initialized_;
        masterVolume_ = other.masterVolume_;
        backend_ = other.backend_;
        liveBanks_ = other.liveBanks_;
        other.context_ = nullptr;
        other.engine_ = nullptr;
        other.initialized_ = false;
        other.liveBanks_ = 0;
    }
    return *this;
}

void AudioContext::teardown() noexcept
{
    if (!initialized_)
    {
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
        (void)ma_engine_uninit(engine_);
        delete engine_;
        engine_ = nullptr;
    }
    if (context_ != nullptr)
    {
        (void)ma_context_uninit(context_);
        delete context_;
        context_ = nullptr;
    }
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
    result.context_ = new ma_context;
    result.engine_ = new ma_engine;

    ma_context_config contextConfig = ma_context_config_init();

    ma_result contextResult = MA_SUCCESS;
    if (config.backend == Config::Backend::Null)
    {
        // Force the null backend: no audio hardware required. This is
        // the headless-CI path; the sandbox uses Default.
        const ma_backend nullBackend = ma_backend_null;
        contextResult = ma_context_init(&nullBackend, 1, &contextConfig, result.context_);
    }
    else
    {
        contextResult = ma_context_init(nullptr, 0, &contextConfig, result.context_);
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
    engineConfig.pContext = result.context_;
    engineConfig.sampleRate = config.sampleRate;
    engineConfig.channels = config.channels;

    const ma_result engineResult = ma_engine_init(&engineConfig, result.engine_);
    if (engineResult != MA_SUCCESS)
    {
        (void)ma_context_uninit(result.context_);
        delete result.context_;
        result.context_ = nullptr;
        delete result.engine_;
        result.engine_ = nullptr;
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
        (void)ma_engine_set_volume(engine_, masterVolume_);
    }
}

} // namespace starforge::audio
