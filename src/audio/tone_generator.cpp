#include "tone_generator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace starforge::audio
{
namespace
{

constexpr float kPi = std::numbers::pi_v<float>;

void appendU16LE(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendU32LE(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void appendFourCC(std::vector<std::uint8_t>& bytes, const char (&cc)[5])
{
    for (std::size_t i = 0; i < 4; ++i)
    {
        bytes.push_back(static_cast<std::uint8_t>(cc[i]));
    }
}

} // namespace

std::vector<std::int16_t> generateTonePcm16(const ToneParams& params)
{
    const std::uint32_t safeRate = params.sampleRate == 0U ? 48000U : params.sampleRate;
    const std::uint32_t safeChannels = params.channels == 0U ? 2U : params.channels;
    const std::size_t frames = static_cast<std::size_t>(
        static_cast<float>(safeRate) * params.durationSeconds);
    std::vector<std::int16_t> pcm(frames * safeChannels, 0);
    const float amplitude = std::clamp(params.amplitude, 0.0f, 1.0f);
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        const float phase = 2.0f * kPi * params.frequencyHz * static_cast<float>(frame) /
                            static_cast<float>(safeRate);
        const auto sample = static_cast<std::int16_t>(std::lround(amplitude * 32767.0f * std::sin(phase)));
        for (std::uint32_t channel = 0; channel < safeChannels; ++channel)
        {
            pcm[frame * safeChannels + channel] = sample;
        }
    }
    return pcm;
}

std::vector<std::uint8_t> generateToneWav(const ToneParams& params)
{
    const std::vector<std::int16_t> pcm = generateTonePcm16(params);
    const std::uint32_t safeRate = params.sampleRate == 0U ? 48000U : params.sampleRate;
    const std::uint32_t safeChannels = params.channels == 0U ? 2U : params.channels;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    constexpr std::uint16_t kBitsPerSample = 16U;
    constexpr std::uint16_t kFormatPcm = 1U;

    std::vector<std::uint8_t> wav;
    wav.reserve(44U + dataBytes);
    appendFourCC(wav, "RIFF");
    appendU32LE(wav, 36U + dataBytes);
    appendFourCC(wav, "WAVE");
    appendFourCC(wav, "fmt ");
    appendU32LE(wav, 16U); // fmt chunk size (PCM)
    appendU16LE(wav, kFormatPcm);
    appendU16LE(wav, static_cast<std::uint16_t>(safeChannels));
    appendU32LE(wav, safeRate);
    appendU32LE(wav, safeRate * safeChannels * (kBitsPerSample / 8U)); // byte rate
    appendU16LE(wav, static_cast<std::uint16_t>(safeChannels * (kBitsPerSample / 8U))); // block align
    appendU16LE(wav, kBitsPerSample);
    appendFourCC(wav, "data");
    appendU32LE(wav, dataBytes);
    for (const std::int16_t sample : pcm)
    {
        appendU16LE(wav, static_cast<std::uint16_t>(sample));
    }
    return wav;
}

} // namespace starforge::audio
