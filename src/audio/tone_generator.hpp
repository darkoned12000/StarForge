#pragma once

// Procedural test-tone generation (M12.2): pure CPU helpers with no
// miniaudio dependency, so tests can fabricate sounds without shipping
// binary audio assets. The asset pipeline (M17) replaces these with
// real audio files; the SoundBank decode path stays the same.

#include <cstdint>
#include <vector>

namespace starforge::audio
{

struct ToneParams
{
    std::uint32_t sampleRate = 48000;
    std::uint32_t channels = 2;
    float frequencyHz = 440.0f;
    float durationSeconds = 0.1f;
    float amplitude = 0.5f; // Peak in [0, 1].
};

// Generates interleaved signed-16-bit PCM samples (sine, zero-phase start).
[[nodiscard]] std::vector<std::int16_t> generateTonePcm16(const ToneParams& params);

// Wraps the PCM in a canonical 44-byte RIFF/WAVE header (16-bit PCM).
// The result is directly decodable by SoundBank::loadFromMemory.
[[nodiscard]] std::vector<std::uint8_t> generateToneWav(const ToneParams& params);

} // namespace starforge::audio
