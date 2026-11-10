#pragma once

#include "Nrpn.h"
#include "Params.h"

#include <optional>
#include <vector>

namespace peak
{
class Patch;

// CC/NRPN messages that set one parameter on the Peak's edit buffer.
std::vector<ShortMessage> encodeParam (const ParamDef&, int value, int channel);

// Messages that load a whole patch into the edit buffer without touching any
// memory slot. Stored parameters are sent in dump order, which also puts
// Reverb Type before Reverb Size (setting Type overwrites Size on the Peak).
std::vector<ShortMessage> encodePatch (const Patch&, int channel);

struct ParamChange
{
    const ParamDef* param = nullptr; // null: an NRPN that isn't a patch parameter (see nrpn)
    int value = 0;
    int nrpn = -1;                    // the NRPN number (msb << 7 | lsb), when it came as one
};

// Turns the Peak's outgoing CC/NRPN stream back into parameter changes.
// Only messages on the given channel are considered.
class ParamMidiDecoder
{
public:
    std::optional<ParamChange> feed (int channel, uint8_t status, uint8_t data1, uint8_t data2);

private:
    NrpnDecoder nrpn;
    int pendingMsb[128];
    bool initialised = false;
};
} // namespace peak
