#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace peak
{
// A WAV file's audio, mixed to mono. Used when the usual reader gives up: some wavetables (14 of
// Serum 2's, e.g. "AT Alter") declare a data chunk longer than the file, which that reader takes
// as empty; here the data runs to the end of the file instead, as macOS reads it.
// PCM 8/16/24/32-bit and float 32/64, plain or WAVE_FORMAT_EXTENSIBLE.
struct WavAudio
{
    std::vector<float> samples;
    double sampleRate = 0.0;
};
std::optional<WavAudio> readWav (const uint8_t* data, size_t size);
} // namespace peak
