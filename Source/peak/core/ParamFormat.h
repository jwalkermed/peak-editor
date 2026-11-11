#pragma once

#include "Params.h"

#include <string>
#include <vector>

namespace peak
{
// Text for a raw parameter value as the Peak's display would show it, using the
// display ranges from the User Guide chart and menu descriptions.
std::string formatValue (const ParamDef&, int value);

// The 10 user wavetables' names (WaveMore 64-73), "User 1".."User 10" until read from the Peak.
std::vector<std::string>& userWavetableNames();

// The display text of every value from min to max, for parameters whose values are names
// (a drop-down); empty for numeric parameters.
std::vector<std::string> valueNames (const ParamDef&);

// Raw value a double-click resets to: centre for bipolar parameters, else minimum.
int defaultValue (const ParamDef&);

// Patch Category names (NRPN 0:0, byte 0x20), as listed in the Peak's patch menu.
const std::vector<std::string>& categoryNames();

// Mod matrix lists in the Peak's menu order (value = index), verified on firmware 2.2.
const std::vector<std::string>& modSourceNames();      // 0-23
const std::vector<std::string>& modDestinationNames(); // 0-47
const std::vector<std::string>& fxSourceNames();       // 0-16
const std::vector<std::string>& fxDestinationNames();  // 0-23
} // namespace peak
