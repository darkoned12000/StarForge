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
    const float safeDuration =
        std::isfinite(params.durationSeconds) ? std::max(0.0f, params.durationSeconds) : 0.0f;
    const std::size_t frames =
        static_cast<std::size_t>(static_cast<float>(safeRate) * safeDuration);
    std::vector<std::int16_t> pcm(frames * safeChannels, 0);
    const float amplitude = std::clamp(std::isfinite(params.amplitude) ? params.amplitude : 0.0f, 0.0f, 1.0f);
    const float startFreq = std::isfinite(params.frequencyHz) ? params.frequencyHz : 0.0f;
    const float endFreq = (params.endFrequencyHz != 0.0f && std::isfinite(params.endFrequencyHz))
                              ? params.endFrequencyHz
                              : startFreq;
    const float attack = std::isfinite(params.attackSeconds) ? std::max(0.0f, params.attackSeconds) : 0.0f;
    const float release = std::isfinite(params.releaseSeconds) ? std::max(0.0f, params.releaseSeconds) : 0.0f;

    // Deterministic LCG for noise so tests are reproducible.
    std::uint32_t noiseState = 0xC0FFEEu;

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        const float t = static_cast<float>(frame) / static_cast<float>(safeRate);
        const float progress = frames > 1 ? static_cast<float>(frame) / static_cast<float>(frames - 1) : 0.0f;
        const float freq = startFreq + (endFreq - startFreq) * progress;
        // Linear sweep phase: 2*pi*(f0*t + (f1-f0)*t^2/(2*T))
        const float phase = 2.0f * kPi * (startFreq * t + (endFreq - startFreq) * t * t / (2.0f * std::max(safeDuration, 1e-6f)));

        float wave = 0.0f;
        switch (params.waveform)
        {
        case Waveform::Sine:
            wave = std::sin(phase);
            break;
        case Waveform::Square:
            wave = std::sin(phase) >= 0.0f ? 1.0f : -1.0f;
            break;
        case Waveform::Saw: {
            const float cycles = freq * t; // approximate for non-sweep
            const float p = phase / (2.0f * kPi);
            wave = 2.0f * (p - std::floor(p + 0.5f));
            (void)cycles;
            break;
        }
        case Waveform::Triangle: {
            const float p = phase / (2.0f * kPi);
            const float frac = p - std::floor(p);
            wave = 4.0f * std::abs(frac - 0.5f) - 1.0f;
            break;
        }
        case Waveform::Noise:
            noiseState = noiseState * 1664525u + 1013904223u;
            wave = static_cast<float>(static_cast<int32_t>(noiseState)) / 2147483648.0f;
            break;
        }

        float envelope = 1.0f;
        if (attack > 0.0f)
        {
            envelope = std::min(envelope, t / attack);
        }
        if (release > 0.0f)
        {
            const float timeToEnd = safeDuration - t;
            envelope = std::min(envelope, timeToEnd / release);
        }
        envelope = std::clamp(envelope, 0.0f, 1.0f);

        const float scaled = std::clamp(amplitude * envelope * wave, -1.0f, 1.0f);
        const auto sample = static_cast<std::int16_t>(std::lround(scaled * 32767.0f));
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
