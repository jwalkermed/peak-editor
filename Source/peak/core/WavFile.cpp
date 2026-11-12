#include "WavFile.h"

#include <algorithm>
#include <cstring>

namespace peak
{
namespace
{
uint32_t le32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t le16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

float sampleAt (const uint8_t* p, int format, int bits)
{
    if (format == 3)
    {
        if (bits == 32)
        {
            float f;
            const uint32_t u = le32 (p);
            std::memcpy (&f, &u, 4);
            return f;
        }
        uint64_t u = (uint64_t) le32 (p) | ((uint64_t) le32 (p + 4) << 32);
        double d;
        std::memcpy (&d, &u, 8);
        return (float) d;
    }
    switch (bits)
    {
        case 8: return ((float) p[0] - 128.0f) / 128.0f;
        case 16: return (float) (int16_t) le16 (p) / 32768.0f;
        case 24: return (float) ((int32_t) (((uint32_t) p[0] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8) / 8388608.0f;
        case 32: return (float) (int32_t) le32 (p) / 2147483648.0f;
        default: return 0.0f;
    }
}
} // namespace

std::optional<WavAudio> readWav (const uint8_t* d, size_t size)
{
    if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
        return std::nullopt;

    int format = 0, channels = 0, bits = 0;
    double rate = 0.0;
    size_t pos = 12;
    while (pos + 8 <= size)
    {
        const auto length = (size_t) le32 (d + pos + 4);
        const auto* body = d + pos + 8;
        const auto available = size - (pos + 8);
        if (std::memcmp (d + pos, "fmt ", 4) == 0 && available >= 16)
        {
            format = le16 (body);
            channels = le16 (body + 2);
            rate = le32 (body + 4);
            bits = le16 (body + 14);
            if (format == 0xFFFE && available >= 26)
                format = le16 (body + 24); // the sub-format's first two bytes: 1 = PCM, 3 = float
        }
        else if (std::memcmp (d + pos, "data", 4) == 0)
        {
            const bool ok = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (format == 3 && (bits == 32 || bits == 64));
            if (! ok || channels < 1 || rate <= 0.0)
                return std::nullopt;
            const auto frameBytes = (size_t) channels * (size_t) (bits / 8);
            const auto frames = std::min (length, available) / frameBytes; // to the end of the file at most
            WavAudio out;
            out.sampleRate = rate;
            out.samples.resize (frames);
            for (size_t f = 0; f < frames; ++f)
            {
                float sum = 0.0f;
                for (int c = 0; c < channels; ++c)
                    sum += sampleAt (body + f * frameBytes + (size_t) c * (size_t) (bits / 8), format, bits);
                out.samples[f] = sum / (float) channels;
            }
            return out;
        }
        pos += 8 + length + (length & 1);
    }
    return std::nullopt;
}
} // namespace peak
