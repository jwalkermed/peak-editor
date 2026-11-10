#pragma once

#include "Sysex.h"

#include <optional>
#include <string>
#include <vector>

namespace peak
{
// The Peak's global settings (Settings menu), verified on firmware 2.2. Each is NRPN 64:n,
// and a Settings dump (Backup page, Select = Settings) holds it at byte 16 + n.
struct SettingDef
{
    const char* id;
    const char* name; // as on the Peak's display
    const char* page; // the Settings page title
    int nrpn;         // n in NRPN 64:n
    int min, max;
    std::vector<std::string> names; // value names (index = value - min), or empty
    int centre = -1;                // shown as a signed offset from this raw value
    bool readOnly = false;          // shown but not editable (changing it could cut the editor off)
};

const std::vector<SettingDef>& allSettings();
const SettingDef* findSetting (const std::string& id);
std::string formatSetting (const SettingDef&, int value);

// Local control is a setting the Peak sets with CC 122 (0 = Off, 127 = On), not an NRPN.
constexpr int localControlCc = 122;

class SettingsDump
{
public:
    static constexpr size_t dumpSize = 527;
    static constexpr uint8_t typeByte = 0x04; // byte 8: 0 current sound, 1 stored patch, 4 settings

    static bool isSettingsDump (const uint8_t* data, size_t size);
    static std::optional<SettingsDump> fromSysex (const Bytes&);

    int get (const SettingDef& s) const { return raw[16 + (size_t) s.nrpn]; }
    void set (const SettingDef& s, int value) { raw[16 + (size_t) s.nrpn] = static_cast<uint8_t> (value); }
    const Bytes& bytes() const noexcept { return raw; }

private:
    Bytes raw;
};
} // namespace peak
