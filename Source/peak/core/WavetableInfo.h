#pragma once

namespace peak
{
// A listener's notes on a factory wavetable, for the picker's hover cards.
struct WavetableInfo
{
    const char* description;
    const char* texture; // texture / character
    const char* usage;
};

// The notes for a WaveMore value (4-63: the 60 factory wavetables), or nullptr (user wavetables).
const WavetableInfo* wavetableInfo (int waveMore);
} // namespace peak
