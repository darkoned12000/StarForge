// Public audio facade tests. Uses only the public headers; the silent
// backend needs no audio hardware.
#include <doctest/doctest.h>

#include "starforge/audio.hpp"
#include "starforge/math.hpp"
#include "starforge/world.hpp"

#include <string>
#include <type_traits>
#include <utility>

namespace
{

[[nodiscard]] starforge::Audio::CreateInfo silentInfo()
{
    starforge::Audio::CreateInfo info;
    info.backend = starforge::Audio::CreateInfo::Backend::Silent;
    return info;
}

[[nodiscard]] std::vector<std::uint8_t> toneWav()
{
    return starforge::generateToneWav(48000, 2, 440.0f, 0.1f, 0.5f);
}

} // namespace

TEST_CASE("Public Audio: is move-only")
{
    static_assert(!std::is_copy_constructible_v<starforge::Audio>);
    static_assert(!std::is_copy_assignable_v<starforge::Audio>);
    static_assert(std::is_move_constructible_v<starforge::Audio>);
    static_assert(std::is_move_assignable_v<starforge::Audio>);
    CHECK(true);
}

TEST_CASE("Public Audio: creates silently, loads, plays, stops")
{
    auto audio = starforge::Audio::create(silentInfo());
    REQUIRE_MESSAGE(audio.has_value(), audio.error());
    CHECK(audio->isValid());
    CHECK(audio->isRunning());
    CHECK(audio->sampleRate() == 48000U);
    CHECK(audio->channels() == 2U);

    audio->setMasterVolume(0.5f);
    CHECK(audio->masterVolume() == doctest::Approx(0.5f));
    audio->setBusVolume(starforge::Audio::Bus::Music, 0.25f);
    CHECK(audio->busVolume(starforge::Audio::Bus::Music) == doctest::Approx(0.25f));

    auto handle = audio->loadFromMemory(toneWav());
    REQUIRE_MESSAGE(handle.has_value(), handle.error());
    CHECK(handle->isValid());
    CHECK(!audio->isPlaying(*handle));
    REQUIRE(audio->play(*handle));
    CHECK(audio->isPlaying(*handle));
    REQUIRE(audio->stop(*handle));
    CHECK(!audio->isPlaying(*handle));
}

TEST_CASE("Public Audio: malformed audio is reported as an API error")
{
    auto audio = starforge::Audio::create(silentInfo());
    REQUIRE_MESSAGE(audio.has_value(), audio.error());

    const std::uint8_t garbage[] = {0U, 1U, 2U, 3U};
    auto rejected = audio->loadFromMemory(garbage);
    CHECK(!rejected.has_value());
    CHECK(!audio->loadFromMemory({}).has_value());
}

TEST_CASE("Public Audio: listener and emitter are camera-relative")
{
    auto audio = starforge::Audio::create(silentInfo());
    REQUIRE_MESSAGE(audio.has_value(), audio.error());

    auto handle = audio->loadFromMemory(toneWav());
    REQUIRE(handle.has_value());

    const auto camera = starforge::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto origin = starforge::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto emitterPos = starforge::WorldPosition::fromMeters(10.0, 0.0, 0.0);
    REQUIRE(camera.has_value());
    REQUIRE(origin.has_value());
    REQUIRE(emitterPos.has_value());

    starforge::Audio::ListenerState listener;
    listener.position = *origin;
    audio->setListener(listener, *camera);

    starforge::Audio::EmitterState emitter;
    emitter.position = *emitterPos;
    CHECK(audio->setEmitter(*handle, emitter, *camera));
    CHECK(audio->setEmitterAttenuation(*handle, 2.0f, 500.0f, 1.5f));

    // UI buses reject emitter poses (non-spatial by design).
    starforge::Audio::SoundParams uiParams;
    uiParams.bus = starforge::Audio::Bus::UI;
    auto uiHandle = audio->loadFromMemory(toneWav(), uiParams);
    REQUIRE(uiHandle.has_value());
    CHECK(!audio->setEmitter(*uiHandle, emitter, *camera));
}

TEST_CASE("Public Audio: voice volume, pitch, and loop controls")
{
    auto audio = starforge::Audio::create(silentInfo());
    REQUIRE_MESSAGE(audio.has_value(), audio.error());

    auto handle = audio->loadFromMemory(toneWav());
    REQUIRE(handle.has_value());
    CHECK(audio->volume(*handle) == doctest::Approx(1.0f));
    CHECK(audio->pitch(*handle) == doctest::Approx(1.0f));
    CHECK(!audio->isLooping(*handle));

    REQUIRE(audio->setVolume(*handle, 0.5f));
    CHECK(audio->volume(*handle) == doctest::Approx(0.5f));
    REQUIRE(audio->setPitch(*handle, 1.5f));
    CHECK(audio->pitch(*handle) == doctest::Approx(1.5f));
    REQUIRE(audio->setLoop(*handle, true));
    CHECK(audio->isLooping(*handle));

    CHECK(!audio->setVolume(starforge::Audio::VoiceHandle{}, 0.5f));
    CHECK(audio->volume(starforge::Audio::VoiceHandle{}) == doctest::Approx(0.0f));
}

TEST_CASE("Public Audio: moved-from instance is inert")
{
    auto audio = starforge::Audio::create(silentInfo());
    REQUIRE(audio.has_value());
    auto moved = std::move(*audio);
    CHECK(!audio->isValid());
    CHECK(!audio->isRunning());
    CHECK(moved.isValid());
    CHECK(moved.isRunning());

    auto rejected = audio->loadFromMemory(toneWav());
    CHECK(!rejected.has_value());
    CHECK(!audio->play(starforge::Audio::VoiceHandle{}));
}
