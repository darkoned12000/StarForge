// Audio layer tests (M12.1/12.2): AudioContext on the null backend and
// SoundBank 2D playback. All tests run headless — no audio hardware
// required (ma_backend_null).
#include <doctest/doctest.h>

#include "audio/audio_context.hpp"
#include "audio/sound.hpp"
#include "audio/tone_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <type_traits>

using namespace starforge::audio;

namespace
{

std::expected<AudioContext, std::string> createNullContext()
{
    AudioContext::Config config;
    config.backend = AudioContext::Config::Backend::Null;
    return AudioContext::create(config);
}

} // namespace

TEST_CASE("AudioContext: move-only RAII semantics")
{
    static_assert(!std::is_copy_constructible_v<AudioContext>);
    static_assert(!std::is_copy_assignable_v<AudioContext>);
    static_assert(std::is_move_constructible_v<AudioContext>);
    static_assert(std::is_move_assignable_v<AudioContext>);
    static_assert(!std::is_copy_constructible_v<SoundBank>);

    AudioContext movedFrom;
    CHECK(!movedFrom.isValid());
}

TEST_CASE("AudioContext: creates on null backend and reports device format")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    CHECK(context->isValid());
    CHECK(context->isRunning());
    CHECK(context->sampleRate() > 0U);
    CHECK(context->channels() != 0U);
    CHECK(std::string(context->backendName()) == "null");
    // Construction/destruction scope exit must be clean.
}

TEST_CASE("AudioContext: rejects invalid configuration")
{
    AudioContext::Config zeroRate;
    zeroRate.sampleRate = 0U;
    auto rejected = AudioContext::create(zeroRate);
    CHECK(!rejected.has_value());

    AudioContext::Config zeroChannels;
    zeroChannels.channels = 0U;
    auto rejectedChannels = AudioContext::create(zeroChannels);
    CHECK(!rejectedChannels.has_value());
}

TEST_CASE("AudioContext: master volume clamps to [0, 1]")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    CHECK(context->masterVolume() == doctest::Approx(1.0f));

    context->setMasterVolume(0.25f);
    CHECK(context->masterVolume() == doctest::Approx(0.25f));

    context->setMasterVolume(2.0f);
    CHECK(context->masterVolume() == doctest::Approx(1.0f));

    context->setMasterVolume(-1.0f);
    CHECK(context->masterVolume() == doctest::Approx(0.0f));
}

TEST_CASE("AudioContext: moved context owns the engine, source is empty")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    AudioContext moved = std::move(*context);
    CHECK(moved.isValid());
    CHECK(moved.isRunning());
    CHECK(!context->isValid());
}

TEST_CASE("Tone generator: PCM shape and sine peak")
{
    ToneParams params;
    params.sampleRate = 48000U;
    params.channels = 2U;
    params.frequencyHz = 440.0f;
    params.durationSeconds = 0.1f;
    params.amplitude = 0.5f;
    const auto pcm = generateTonePcm16(params);
    // 0.1s at 48 kHz = 4800 frames, interleaved stereo.
    CHECK(pcm.size() == 4800U * 2U);

    const auto peak = std::abs(*std::max_element(pcm.begin(), pcm.end(), [](std::int16_t a, std::int16_t b) {
        return std::abs(a) < std::abs(b);
    }));
    // Sine peak near 0.5 * 32767.
    CHECK(peak > 15000);
    CHECK(peak < 17000);
}

TEST_CASE("Tone generator: WAV header is canonical and well-sized")
{
    ToneParams params;
    const auto wav = generateToneWav(params);
    REQUIRE(wav.size() > 44U);
    // RIFF/WAVE/fmt /data magic plus consistent chunk sizes.
    CHECK(wav[0] == 'R');
    CHECK(wav[1] == 'I');
    CHECK(wav[2] == 'F');
    CHECK(wav[3] == 'F');
    CHECK(wav[8] == 'W');
    CHECK(wav[9] == 'A');
    CHECK(wav[10] == 'V');
    CHECK(wav[11] == 'E');
    const std::uint32_t dataBytes =
        static_cast<std::uint32_t>(wav[40]) |
        (static_cast<std::uint32_t>(wav[41]) << 8U) | (static_cast<std::uint32_t>(wav[42]) << 16U) |
        (static_cast<std::uint32_t>(wav[43]) << 24U);
    CHECK(dataBytes + 44U == wav.size());
}

TEST_CASE("SoundBank: loads, plays, stops, and loops a tone")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    const auto wav = generateToneWav({});
    SoundConfig config;
    config.bus = Bus::SFX;
    config.loop = true;
    auto handle = bank.loadFromMemory(wav, config);
    REQUIRE(handle.has_value());
    CHECK(handle->isValid());
    CHECK(bank.soundCount() == 1U);
    CHECK(!bank.isPlaying(*handle));
    CHECK(bank.isLooping(*handle));

    REQUIRE(bank.play(*handle));
    CHECK(bank.isPlaying(*handle));

    REQUIRE(bank.stop(*handle));
    CHECK(!bank.isPlaying(*handle));

    // Unlooped reconfiguration takes effect immediately.
    REQUIRE(bank.setLoop(*handle, false));
    CHECK(!bank.isLooping(*handle));
}

TEST_CASE("SoundBank: volume and pitch round-trip")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    auto handle = bank.loadFromMemory(generateToneWav({}), {});
    REQUIRE(handle.has_value());
    CHECK(bank.volume(*handle) == doctest::Approx(1.0f));
    REQUIRE(bank.setVolume(*handle, 0.5f));
    CHECK(bank.volume(*handle) == doctest::Approx(0.5f));
    REQUIRE(bank.setPitch(*handle, 1.5f));
    CHECK(bank.pitch(*handle) == doctest::Approx(1.5f));
    // Non-positive pitch is rejected.
    CHECK(!bank.setPitch(*handle, 0.0f));
}

TEST_CASE("SoundBank: malformed and empty data are rejected")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    const std::uint8_t garbage[] = {0U, 1U, 2U, 3U, 4U, 5U};
    auto rejected = bank.loadFromMemory(garbage, {});
    CHECK(!rejected.has_value());
    CHECK(bank.loadFromMemory({}, {}).has_value() == false);
    CHECK(bank.soundCount() == 0U);
}

TEST_CASE("SoundBank: invalid handles are safe no-ops")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    SoundHandle invalid;
    CHECK(!invalid.isValid());
    CHECK(!bank.play(invalid));
    CHECK(!bank.stop(invalid));
    CHECK(!bank.isPlaying(invalid));
    CHECK(!bank.setVolume(invalid, 0.5f));
    CHECK(bank.volume(invalid) == doctest::Approx(0.0f));
}

TEST_CASE("SoundBank: bus volumes are independent, Master routes to context")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    bank.setBusVolume(Bus::Music, 0.25f);
    bank.setBusVolume(Bus::SFX, 0.75f);
    bank.setBusVolume(Bus::UI, 0.5f);
    bank.setBusVolume(Bus::Voice, 0.1f);
    CHECK(bank.busVolume(Bus::Music) == doctest::Approx(0.25f));
    CHECK(bank.busVolume(Bus::SFX) == doctest::Approx(0.75f));
    CHECK(bank.busVolume(Bus::UI) == doctest::Approx(0.5f));
    CHECK(bank.busVolume(Bus::Voice) == doctest::Approx(0.1f));

    bank.setBusVolume(Bus::Master, 0.4f);
    CHECK(bank.busVolume(Bus::Master) == doctest::Approx(0.4f));
    CHECK(context->masterVolume() == doctest::Approx(0.4f));
}

TEST_CASE("SoundBank: create on invalid context fails cleanly (no zombie bank)")
{
    AudioContext invalidContext;
    auto bank = SoundBank::create(invalidContext);
    CHECK(!bank.has_value());
}

TEST_CASE("SoundBank: loaded sound survives source buffer destruction (UAF regression)")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    // Load from a heap buffer that is destroyed before playback: the
    // bank must retain its own copy of the encoded bytes.
    SoundHandle handle;
    {
        std::vector<std::uint8_t> source = generateToneWav({});
        auto loaded = bank.loadFromMemory(source, {});
        REQUIRE(loaded.has_value());
        handle = *loaded;
    } // source freed here
    REQUIRE(bank.play(handle));
    CHECK(bank.isPlaying(handle));
    REQUIRE(bank.stop(handle));
}

TEST_CASE("SoundBank: finished one-shot rewinds on next play (replay regression)")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    // Very short one-shot so the null backend reaches EOF quickly.
    ToneParams params;
    params.durationSeconds = 0.02f;
    auto handle = bank.loadFromMemory(generateToneWav(params), {});
    REQUIRE(handle.has_value());

    REQUIRE(bank.play(*handle));
    // Wait for the 20 ms clip to finish on the mixing thread (bounded).
    for (int i = 0; i < 200 && !bank.isAtEnd(*handle); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(bank.isAtEnd(*handle));
    // Replay must rewind and produce sound again.
    REQUIRE(bank.play(*handle));
    CHECK(bank.isPlaying(*handle));
    REQUIRE(bank.stop(*handle));
}

TEST_CASE("SoundBank: UI and Music buses are non-spatial, SFX stays spatial")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    auto bankOrError = SoundBank::create(*context);
    REQUIRE(bankOrError.has_value());
    SoundBank& bank = *bankOrError;

    SoundConfig uiConfig;
    uiConfig.bus = Bus::UI;
    auto uiHandle = bank.loadFromMemory(generateToneWav({}), uiConfig);
    REQUIRE(uiHandle.has_value());
    CHECK(!bank.isSpatializationEnabled(*uiHandle));

    SoundConfig musicConfig;
    musicConfig.bus = Bus::Music;
    auto musicHandle = bank.loadFromMemory(generateToneWav({}), musicConfig);
    REQUIRE(musicHandle.has_value());
    CHECK(!bank.isSpatializationEnabled(*musicHandle));

    SoundConfig sfxConfig;
    sfxConfig.bus = Bus::SFX;
    auto sfxHandle = bank.loadFromMemory(generateToneWav({}), sfxConfig);
    REQUIRE(sfxHandle.has_value());
    CHECK(bank.isSpatializationEnabled(*sfxHandle));
}

TEST_CASE("SoundBank: context tracks live banks and enforces teardown order")
{
    auto context = createNullContext();
    REQUIRE(context.has_value());
    CHECK(context->liveBankCount() == 0U);
    {
        auto bank = SoundBank::create(*context);
        REQUIRE(bank.has_value());
        CHECK(context->liveBankCount() == 1U);
    }
    CHECK(context->liveBankCount() == 0U);
}
