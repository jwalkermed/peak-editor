#pragma once

#include <string_view>
#include <vector>

namespace peak
{
enum class MidiKind
{
    cc,     // one 7-bit CC
    ccPair, // 8-bit value: CC n carries value >> 1, CC n+32 carries (value & 1) << 6
    nrpn    // CC 99/98 select, CC 6 data (7-bit; the Peak uses CC 38 as a parameter, not data LSB)
};

// One Peak sound parameter. The table is generated from docs/peak-patch-map.json
// by tools/gen_params.py; nothing else should hard-code parameter numbers.
struct ParamDef
{
    const char* id;      // stable key, e.g. "osc1_coarse"
    const char* name;    // display name
    const char* section; // front-panel / menu grouping
    MidiKind kind;
    int number;          // CC number (MSB for pairs) or NRPN number (msb << 7 | lsb)
    int min, max;        // raw MIDI value range, as in the User Guide chart
    int offset;          // byte index in the 527-byte patch dump, or -1 if not stored

    bool isStored() const noexcept { return offset >= 0; }
};

const std::vector<ParamDef>& allParams();
const ParamDef* findParam (std::string_view id);
int indexOf (const ParamDef&);
} // namespace peak
