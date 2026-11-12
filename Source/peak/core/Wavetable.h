#pragma once

#include "Sysex.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace peak
{
// A Peak user wavetable: a name (up to 7 characters) and 5 waves. The Peak keeps each wave as a
// spectrum: a DC value, then harmonics 1-128, each a cosine and a sine part (see docs/sysex-notes.md,
// "User wavetable data"). Values here are -1..1 (the Peak's full scale is 8191).
struct Wave
{
    static constexpr int harmonics = 128, samplesPerCycle = 256;
    std::array<float, harmonics + 1> sine {}, cosine {}; // index 0 unused
    float dc = 0.0f;                                     // the offset (the message's first value)

    // One cycle of samples from the harmonics, and harmonics from one cycle of samples (a DFT;
    // any length, resampled to 256 first). At 256 samples the two are exact inverses.
    std::vector<float> samples (int count = samplesPerCycle) const;
    static Wave fromSamples (const std::vector<float>& cycle);

    float level (int h) const;  // magnitude of harmonic h
    float phase (int h) const;  // its phase, -pi..pi (0 = sine)
    void setLevel (int h, float level);
    void setPhase (int h, float phase);

    bool operator== (const Wave&) const = default;
};

struct Wavetable
{
    static constexpr int waveCount = 5, slots = 10, nameLength = 7;
    std::string name = "Init";
    std::array<Wave, waveCount> waves;

    Wavetable(); // every wave a sine, as an empty user slot

    // The messages that upload it to user slot 0-9: the name, then the 5 waves (what Components sends).
    // Each wave is scaled down if any value or point of it goes past full scale.
    std::vector<Bytes> toSysex (int slot) const;
    // From those messages (a .syx file, or the Peak's replies): name (optional) and the 5 waves.
    static std::optional<Wavetable> fromSysex (const std::vector<Bytes>& messages);

    bool operator== (const Wavetable&) const = default;
};

// Requests the Peak answers with one wave (type 06): slot 0-9, wave 0-4.
Bytes wavetableDataRequest (int slot, int wave);
bool isWavetableData (const uint8_t* data, size_t size); // a type 06 wave message (531 bytes)
// The wave in a type 06 message, with its slot and wave number.
std::optional<std::pair<std::pair<int, int>, Wave>> parseWavetableData (const Bytes&);

namespace Shapes
{
enum Kind { sine, square, pulse, saw, triangle, noise };
constexpr float pulseWidth = 0.125f; // the pulse is high for this much of the cycle
float at (Kind, float phase, unsigned& seed); // one cycle, phase 0-1, -1..1 (seed: for noise)
Wave make (Kind, unsigned seed = 1);           // full height
}

namespace WaveOps
{
Wave normalise (Wave);  // loudest point at full scale
Wave reverse (Wave);    // played backwards
Wave invert (Wave);     // upside down
Wave clear();           // silence
Wave blend (const Wave& a, const Wave& b, float amount); // 0 = a, 1 = b (in the harmonics)
Wave smooth (const Wave&, float amount);                  // 0-1: softens the upper harmonics
}
} // namespace peak
