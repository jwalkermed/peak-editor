#include "Wavetable.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace peak
{
namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr float fullScale = 8191.0f;
const uint8_t header[] { 0xF0, 0x00, 0x20, 0x29, 0x01, 0x10, 0x00, 0x7E };

int toRaw (float v)
{
    return std::clamp ((int) std::lround (v * fullScale), -8192, 8191);
}

// Signed 14-bit as two 7-bit bytes, high first.
void put14 (Bytes& out, int v)
{
    const auto u = static_cast<unsigned> (v < 0 ? v + 16384 : v);
    out.push_back (static_cast<uint8_t> ((u >> 7) & 0x7F));
    out.push_back (static_cast<uint8_t> (u & 0x7F));
}

int get14 (uint8_t hi, uint8_t lo)
{
    const int v = (hi << 7) | lo;
    return v >= 8192 ? v - 16384 : v;
}

std::vector<float> resample (const std::vector<float>& in, int count)
{
    std::vector<float> out ((size_t) count);
    if (in.empty())
        return out;
    for (int i = 0; i < count; ++i)
    {
        const double pos = (double) i * (double) in.size() / count;
        const auto a = (size_t) pos % in.size(), b = (a + 1) % in.size();
        const auto f = (float) (pos - std::floor (pos));
        out[(size_t) i] = in[a] + (in[b] - in[a]) * f;
    }
    return out;
}
} // namespace

std::vector<float> Wave::samples (int count) const
{
    std::vector<float> out ((size_t) count, dc);
    for (int h = 1; h <= harmonics; ++h)
    {
        if (sine[(size_t) h] == 0.0f && cosine[(size_t) h] == 0.0f)
            continue;
        for (int i = 0; i < count; ++i)
        {
            const double x = twoPi * h * i / count;
            out[(size_t) i] += (float) (sine[(size_t) h] * std::sin (x) + cosine[(size_t) h] * std::cos (x));
        }
    }
    return out;
}

Wave Wave::fromSamples (const std::vector<float>& cycle)
{
    const auto s = resample (cycle, samplesPerCycle);
    Wave w;
    for (auto v : s)
        w.dc += v / (float) samplesPerCycle; // the offset is kept, so 256 samples come back exactly as drawn
    for (int h = 1; h <= harmonics; ++h)
    {
        double a = 0.0, b = 0.0;
        for (int i = 0; i < samplesPerCycle; ++i)
        {
            const double x = twoPi * h * i / samplesPerCycle;
            a += s[(size_t) i] * std::sin (x);
            b += s[(size_t) i] * std::cos (x);
        }
        const double scale = h == samplesPerCycle / 2 ? 1.0 : 2.0; // the top harmonic has no mirror
        w.sine[(size_t) h] = (float) (scale * a / samplesPerCycle);
        w.cosine[(size_t) h] = (float) (scale * b / samplesPerCycle);
    }
    return w;
}

float Wave::level (int h) const { return std::hypot (sine[(size_t) h], cosine[(size_t) h]); }
float Wave::phase (int h) const { return std::atan2 (cosine[(size_t) h], sine[(size_t) h]); }

void Wave::setLevel (int h, float l)
{
    const auto p = level (h) > 1.0e-6f ? phase (h) : 0.0f;
    sine[(size_t) h] = l * std::cos (p);
    cosine[(size_t) h] = l * std::sin (p);
}

void Wave::setPhase (int h, float p)
{
    const auto l = level (h);
    sine[(size_t) h] = l * std::cos (p);
    cosine[(size_t) h] = l * std::sin (p);
}

Wavetable::Wavetable()
{
    for (auto& w : waves)
        w = Shapes::make (Shapes::sine);
}

std::vector<Bytes> Wavetable::toSysex (int slot) const
{
    std::vector<Bytes> out;
    Bytes nameMsg (std::begin (header), std::end (header));
    for (auto b : { 0x07, 0, 0, 0, 0, 0 })
        nameMsg.push_back (static_cast<uint8_t> (b));
    nameMsg.push_back (static_cast<uint8_t> (slot & 0x7F));
    for (int i = 0; i < nameLength; ++i)
    {
        const char c = i < (int) name.size() ? name[(size_t) i] : ' ';
        nameMsg.push_back (static_cast<uint8_t> (c >= 32 && c < 127 ? c : ' '));
    }
    nameMsg.push_back (0xF7);
    out.push_back (nameMsg);

    for (int w = 0; w < waveCount; ++w)
    {
        Bytes m (std::begin (header), std::end (header));
        for (auto b : { 0x06, 0, 0, 0, 0, 0 })
            m.push_back (static_cast<uint8_t> (b));
        m.push_back (static_cast<uint8_t> (slot & 0x7F));
        m.push_back (static_cast<uint8_t> (w));
        // Scaled down only if needed, as Components does: so that no value and no point of the wave
        // goes past full scale (a full-height square's first harmonic is 4/pi, for example).
        const auto& wave = waves[(size_t) w];
        float peak = std::abs (wave.dc);
        for (int h = 1; h <= Wave::harmonics; ++h)
            peak = std::max ({ peak, std::abs (wave.sine[(size_t) h]), std::abs (wave.cosine[(size_t) h]) });
        for (auto v : wave.samples())
            peak = std::max (peak, std::abs (v));
        const auto scale = peak > 1.0f ? 1.0f / peak : 1.0f;
        put14 (m, toRaw (wave.dc * scale));
        for (int h = 1; h <= Wave::harmonics; ++h) // the cosine part as the Peak has it (negated), then the sine
        {
            put14 (m, toRaw (-wave.cosine[(size_t) h] * scale));
            put14 (m, toRaw (wave.sine[(size_t) h] * scale));
        }
        m.push_back (0xF7);
        out.push_back (std::move (m));
    }
    return out;
}

bool isWavetableData (const uint8_t* d, size_t n)
{
    return n == 531 && std::equal (std::begin (header), std::end (header), d) && d[8] == 0x06 && d[n - 1] == 0xF7;
}

std::optional<std::pair<std::pair<int, int>, Wave>> parseWavetableData (const Bytes& m)
{
    if (! isWavetableData (m.data(), m.size()))
        return std::nullopt;
    Wave w;
    const auto* b = m.data() + 16;
    w.dc = (float) get14 (b[0], b[1]) / fullScale;
    for (int h = 1; h <= Wave::harmonics; ++h)
    {
        w.cosine[(size_t) h] = -(float) get14 (b[4 * h - 2], b[4 * h - 1]) / fullScale;
        w.sine[(size_t) h] = (float) get14 (b[4 * h], b[4 * h + 1]) / fullScale;
    }
    return std::pair<std::pair<int, int>, Wave> { { m[14], m[15] }, w };
}

std::optional<Wavetable> Wavetable::fromSysex (const std::vector<Bytes>& messages)
{
    Wavetable t;
    t.name.clear();
    std::array<bool, waveCount> got {};
    for (const auto& m : messages)
    {
        if (const auto w = parseWavetableData (m); w && w->first.second < waveCount)
        {
            t.waves[(size_t) w->first.second] = w->second;
            got[(size_t) w->first.second] = true;
        }
        else if (const auto n = parseWavetableName (m))
            t.name = n->second;
    }
    if (! std::all_of (got.begin(), got.end(), [] (bool g) { return g; }))
        return std::nullopt;
    if (t.name.empty())
        t.name = "Import";
    return t;
}

Bytes wavetableDataRequest (int slot, int wave)
{
    return { 0xF0, 0x00, 0x20, 0x29, 0x01, 0x10, 0x00, 0x7E, 0x46, 0x00, 0x00,
             static_cast<uint8_t> (slot & 0x7F), static_cast<uint8_t> (wave & 0x7F), 0x00, 0xF7 };
}

namespace Shapes
{
float at (Kind kind, float p, unsigned& seed)
{
    switch (kind)
    {
        case sine: return (float) std::sin (twoPi * p);
        case square: return p < 0.5f ? 1.0f : -1.0f;
        case pulse: return p < pulseWidth ? 1.0f : -1.0f;
        case saw: return p < 0.5f ? 2.0f * p : 2.0f * p - 2.0f; // rises, then drops
        case triangle: return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
        case noise:
            seed = seed * 1664525u + 1013904223u;
            return (float) (seed >> 8) / (float) (1u << 23) - 1.0f;
    }
    return 0.0f;
}

Wave make (Kind kind, unsigned seed)
{
    // Full height, from 256 samples (so it's drawn back exactly).
    std::vector<float> s ((size_t) Wave::samplesPerCycle);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = at (kind, (float) i / (float) s.size(), seed);
    return Wave::fromSamples (s);
}
} // namespace Shapes

namespace WaveOps
{
Wave normalise (Wave w)
{
    float peak = 0.0f;
    for (auto v : w.samples())
        peak = std::max (peak, std::abs (v));
    if (peak > 1.0e-6f)
    {
        w.dc /= peak;
        for (int h = 1; h <= Wave::harmonics; ++h)
        {
            w.sine[(size_t) h] /= peak;
            w.cosine[(size_t) h] /= peak;
        }
    }
    return w;
}

Wave reverse (Wave w)
{
    for (int h = 1; h <= Wave::harmonics; ++h) // x(-t): the sine parts change sign
        w.sine[(size_t) h] = -w.sine[(size_t) h];
    return w;
}

Wave invert (Wave w)
{
    w.dc = -w.dc;
    for (int h = 1; h <= Wave::harmonics; ++h)
    {
        w.sine[(size_t) h] = -w.sine[(size_t) h];
        w.cosine[(size_t) h] = -w.cosine[(size_t) h];
    }
    return w;
}

Wave clear()
{
    return {};
}

Wave blend (const Wave& a, const Wave& b, float t)
{
    Wave w;
    w.dc = a.dc + (b.dc - a.dc) * t;
    for (int h = 1; h <= Wave::harmonics; ++h)
    {
        w.sine[(size_t) h] = a.sine[(size_t) h] + (b.sine[(size_t) h] - a.sine[(size_t) h]) * t;
        w.cosine[(size_t) h] = a.cosine[(size_t) h] + (b.cosine[(size_t) h] - a.cosine[(size_t) h]) * t;
    }
    return w;
}

Wave smooth (const Wave& in, float amount)
{
    // A gentle low-pass over the harmonics: more smoothing, fewer upper harmonics.
    Wave w = in;
    const auto cutoff = 8.0f + (1.0f - std::clamp (amount, 0.0f, 1.0f)) * 119.0f;
    for (int h = 1; h <= Wave::harmonics; ++h)
    {
        const auto g = 1.0f / (1.0f + std::pow ((float) h / cutoff, 4.0f));
        w.sine[(size_t) h] *= g;
        w.cosine[(size_t) h] *= g;
    }
    return w;
}
} // namespace WaveOps
} // namespace peak
