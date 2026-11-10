#include "Settings.h"

#include <algorithm>

namespace peak
{
const std::vector<SettingDef>& allSettings()
{
    // Names, pages and order from the Peak's own Settings pages; numbers from the menu walk
    // and Settings dump captured on 2026-11-10 (Tests/fixtures/settings-*).
    static const std::vector<SettingDef> table {
        { "protect", "Protect", "System", 9, 0, 1, { "Off", "On" } },
        { "pickup", "Pickup", "System", 10, 0, 1, { "Off", "On" } },
        { "brightness", "Brightness", "System", 11, 0, 127, {} },
        { "msg_time", "Msg Time", "System", 24, 0, 127, {} },
        { "vel_shape", "VelShape", "Synth", 12, 0, 127, {} },
        { "tune_cents", "TuneCents", "Synth", 13, 14, 114, {}, 64 },
        { "transpose", "Transpose", "Synth", 14, 52, 76, {}, 64 },
        { "midi_chan", "MidiChan", "MIDI Control", 15, 0, 15, { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" }, -1, true },
        { "arp_to_midi", "Arp>MIDI", "MIDI Control", 17, 0, 1, { "Off", "On" } },
        { "cc_nrpn", "CC/NRPN", "MIDI Enable", 18, 0, 3, { "Disabled", "Receive", "Transmit", "Rec+Tran" }, -1, true },
        { "bank_patch", "Bank/Patch", "MIDI Enable", 19, 0, 3, { "Disabled", "Receive", "Transmit", "Rec+Tran" } },
        { "ped1_sense", "Ped1Sense", "Pedal Sw Sense", 20, 0, 2, { "Auto", "N/Open", "N/Closed" } },
        { "ped2_sense", "Ped2Sense", "Pedal Sw Sense", 21, 0, 2, { "Auto", "N/Open", "N/Closed" } },
        { "ped1_mode", "Ped1Mode", "Pedal Sw Mode", 22, 0, 2, { "Animate1", "Sustain", "Sostenuto" } },
        { "ped2_mode", "Ped2Mode", "Pedal Sw Mode", 23, 0, 2, { "Animate2", "Sustain", "Sostenuto" } },
        { "vol_range", "VolRange", "Misc Settings", 29, 0, 2, { "-6dB", "-3dB", "0dB" } },
        { "initialise", "Initialise", "Misc Settings", 27, 0, 1, { "IniPatch", "Live" } },
        // TuningMode is new in 2.x (names read from the Peak, 2026-11-10).
        { "tuning_mode", "TuningMode", "Misc Settings", 30, 0, 1, { "Note", "Octave" } },
        { "patch_cue", "PatchCue", "Misc Settings", 0, 0, 1, { "Off", "On" } },
    };
    return table;
}

const SettingDef* findSetting (const std::string& id)
{
    const auto& all = allSettings();
    const auto it = std::find_if (all.begin(), all.end(), [&] (const SettingDef& s) { return id == s.id; });
    return it != all.end() ? &*it : nullptr;
}

std::string formatSetting (const SettingDef& s, int v)
{
    if (! s.names.empty() && v >= s.min && v - s.min < (int) s.names.size())
        return s.names[(size_t) (v - s.min)];
    if (s.centre >= 0)
        return (v > s.centre ? "+" : "") + std::to_string (v - s.centre);
    return std::to_string (v);
}

bool SettingsDump::isSettingsDump (const uint8_t* data, size_t size)
{
    return size == dumpSize && isNovationSysex (data, size) && data[size - 1] == 0xF7 && data[8] == typeByte;
}

std::optional<SettingsDump> SettingsDump::fromSysex (const Bytes& message)
{
    if (! isSettingsDump (message.data(), message.size()))
        return std::nullopt;
    SettingsDump d;
    d.raw = message;
    return d;
}
} // namespace peak
