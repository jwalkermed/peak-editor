#pragma once

#include "Params.h"

#include <string>
#include <vector>

namespace peak
{
// Text for a raw parameter value as the Peak's display would show it, using the
// display ranges from the User Guide chart and menu descriptions.
std::string formatValue (const ParamDef&, int value);

// Raw value a double-click resets to: centre for bipolar parameters, else minimum.
int defaultValue (const ParamDef&);

// Patch Category names (NRPN 0:0, byte 0x20), as listed in the Peak's patch menu.
const std::vector<std::string>& categoryNames();
} // namespace peak
