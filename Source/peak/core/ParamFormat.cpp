#include "ParamFormat.h"

#include <array>
#include <cstring>
#include <regex>
#include <string_view>
#include <vector>

namespace peak
{
namespace
{
bool matches (const ParamDef& p, const char* pattern)
{
    return std::regex_search (p.id, std::regex (pattern));
}

std::string sign (int v)
{
    return v > 0 ? "+" + std::to_string (v) : std::to_string (v);
}

// Bipolar parameters: the raw value that displays as 0.
int centreOf (const ParamDef& p)
{
    if (matches (p, "^osc\\d_(coarse|fine)$|^osc\\d_lfo2_to_pitch$|^lfo1_to_filter$"))
        return 128;
    if (matches (p, "_to_(pitch|shape|filter)$|manual_shape$|_velocity$|^chorus_feedback$|^pan_posn$|^mod\\d+_depth$|^fxmod\\d_depth$"
                    "|^keyboard_octave$|^pre_glide$|_bend_range$"))
        return 64;
    return -1;
}

struct Names
{
    const char* pattern;
    std::vector<const char*> names;
    int base = 0; // raw value of names[0]
};

// Only names confirmed by the User Guide, the firmware addendum or the Peak itself.
const Names enumNames[] {
    // From the Peak's own category list (2026-11-09); matches the factory bank A patches.
    { "^patch_category$", { "None", "Arp", "Bass", "Bell", "Classic", "Drum", "Keyboard", "Lead", "Motion", "Pad", "Poly", "SFX", "String", "User 1", "User 2" } },
    { "^osc\\d_range$", { "16'", "8'", "4'", "2'" }, 63 },
    { "^osc\\d_wave$", { "Sine", "Triangle", "Sawtooth", "Pulse", "More" } },
    { "^osc\\d_shape_source$", { "Manual", "Mod Env 1", "LFO 1" } },
    { "^lfo\\d_wave$", { "Triangle", "Sawtooth", "Square", "S+H" } },
    { "^lfo[12]_range$", { "Low", "High", "Sync" } },
    { "^lfo\\d_fade_in_out$", { "FadeIn", "FadeOut", "GateIn", "GateOut" } },
    { "^lfo\\d_monotrig$", { "Legato", "Re-Trig" } },
    { "^(ampenv|modenv\\d)_trigger$", { "Legato", "Retrigger" } }, // Init: Amp Legato, Mod Envs Re-Trig
    { "^filter_slope$", { "12 dB", "24 dB" } },
    { "^filter_shape$", { "LP", "BP", "HP" } },
    { "^filter_env_select$", { "Amp Env", "Mod Env 1" } },
    { "^spread_mode$", { "Diverge", "Alternate", "Diverge 2", "NoteVal" } },
    { "^chorus_mode$", { "Chorus", "Flanger", "Phlanger" } },
    { "^delay_mode$", { "Original", "CrossFed", "Dual" } },
    { "^delay_output$", { "PreDamp", "PostDamp" } },
    { "^delay_timemode$", { "Normal", "Double", "Treble", "QuadLoFi", "HexVLoFi" } },
    { "^(chorus|reverb)_type$", { "1", "2", "3" } },
    // User Guide menu descriptions; each Init Patch value matches its default.
    { "^voice_mode$", { "Mono", "MonoLG", "Mono2", "Poly", "Poly2" } },
    { "^voice_unison$", { "1", "2", "3", "4", "8" } },
    { "^arp_type$", { "Up", "Down", "Up-Down 1", "Up-Down 2", "Played", "Random", "Chord" } },
    { "^arp_octave$", { "1", "2", "3", "4", "5", "6" } },
    { "^arp_rhythm$", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17",
                        "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33" } },
    { "^fx_routing$", { "Parallel", "D>R>C", "D>C>R", "R>D>C", "R>C>D", "C>D>R", "C>R>D" } },
    // Sync divisions fastest first, as the Init Patch's Arp SyncRate 16th (raw 3) shows; the guide lists them slowest first.
    { "^arp_sync_rate$", { "32nd T", "32nd", "16th T", "16th", "8th T", "16th D", "8th", "4th T",
                        "8th D", "4th", "1 + 1/3", "4th D", "2nd", "2 + 2/3", "3 beats", "4 beats",
                        "5 + 1/3", "6 beats", "8 beats" } },
    // Read from the Peak (2026-11-10): the LFO list, fastest first, up to 24 beats. Delay runs 0-27, not the chart's 0-18.
    { "^delay_sync_time$", { "32nd T", "32nd", "16th T", "16th", "8th T", "16th D", "8th", "4th T",
                        "8th D", "4th", "1 + 1/3", "4th D", "2nd", "2 + 2/3", "3 beats", "4 beats",
                        "5 + 1/3", "6 beats", "8 beats", "10 + 2/3", "12 beats", "13 + 1/3", "16 beats", "18 beats",
                        "18 + 2/3", "20 beats", "21 + 1/3", "24 beats" } },
    // Read from the Peak (2026-11-10): slowest first, 0 = 64 beats; LFO 3/4 add Off at 0.
    { "^lfo[12]_sync_rate$", { "64 beats", "48 beats", "42 beats", "36 beats", "32 beats", "30 beats", "28 beats", "24 beats",
                        "21 + 1/3", "20 beats", "18 + 2/3", "18 beats", "16 beats", "13 + 1/3", "12 beats", "10 + 2/3",
                        "8 beats", "6 beats", "5 + 1/3", "4 beats", "3 beats", "2 + 2/3", "2nd", "4th D",
                        "1 + 1/3", "4th", "8th D", "4th T", "8th", "16th D", "8th T", "16th",
                        "16th T", "32nd", "32nd T" } },
    { "^lfo[34]_ratesync$", { "Off", "64 beats", "48 beats", "42 beats", "36 beats", "32 beats", "30 beats", "28 beats", "24 beats",
                        "21 + 1/3", "20 beats", "18 + 2/3", "18 beats", "16 beats", "13 + 1/3", "12 beats", "10 + 2/3",
                        "8 beats", "6 beats", "5 + 1/3", "4 beats", "3 beats", "2 + 2/3", "2nd", "4th D",
                        "1 + 1/3", "4th", "8th D", "4th T", "8th", "16th D", "8th T", "16th",
                        "16th T", "32nd", "32nd T" } },
    // The 60 wavetables (User Guide p. 37, read down its columns; checked on the Peak 2026-11-10). Raw 4-63.
    { "^osc\\d_wave_more$", { "BS sine", "Random", "Zing", "Tubey", "Octaves", "Wobbler", "Chords", "Didgery",
                        "Harsh", "Organ", "E. Piano", "VoxOooEe", "VoxYahEe", "Winds", "SoftClav", "String",
                        "BassOrgn", "Acid", "Buzzy", "Carousel", "Choral", "Climbing", "CoinFlip", "Deep",
                        "Dub", "Eee", "Eris", "Flame", "Further", "GlassSaw", "Glassy", "Granular",
                        "Grime", "Drow", "Heavy", "Hedge", "Hungry", "Ladders", "Lead", "Modeling",
                        "Modem", "Monster", "Screech", "SeaBase", "Shmorgan", "Spirals", "Steel", "Sunrise",
                        "Swell", "Thicker", "Thinner", "Tides", "Tokyo", "Tops", "V.Chord", "Variance",
                        "Vocaloid", "Vowelled", "WeirdVox", "Yeah" }, 4 },
};
// Display rule for one parameter, worked out once from its id.
struct Format
{
    int centre = -1;                  // bipolar: raw value shown as 0
    const Names* names = nullptr;     // enumerated names
    int namesBase = 0;                // raw value of names[0]
    bool onOff = false, offAtZero = false, offAtCentre = false, percent = false, phase = false, bpm = false;
    const std::vector<std::string>* list = nullptr; // mod matrix source/destination names
};

const Format& formatFor (const ParamDef& p)
{
    static const std::vector<Format> formats = [] {
        std::vector<Format> all;
        for (const auto& q : allParams())
        {
            Format f;
            f.centre = centreOf (q);
            for (const auto& e : enumNames)
                if (matches (q, e.pattern))
                {
                    f.names = &e;
                    f.namesBase = e.base;
                    break;
                }
            f.list = matches (q, "^mod\\d+_source[12]$") ? &modSourceNames()
                   : matches (q, "^mod\\d+_dest$")         ? &modDestinationNames()
                   : matches (q, "^fxmod\\d_source2?$")    ? &fxSourceNames()
                   : matches (q, "^fxmod\\d_dest$")        ? &fxDestinationNames()
                                                            : nullptr;
            f.offAtCentre = matches (q, "^pre_glide$");
            f.offAtZero = matches (q, "(_fixed_note|_repeats)$");
            f.phase = matches (q, "^lfo\\d_phase$");
            f.percent = matches (q, "^arp_(chance|swing)$");
            f.bpm = matches (q, "^clock_rate$");
            f.onOff = q.min == 0 && q.max == 1 && f.names == nullptr && ! matches (q, "^(patch|mod\\d|fxmod)");
            all.push_back (f);
        }
        return all;
    }();
    return formats[(size_t) indexOf (p)];
}
} // namespace

std::vector<std::string> valueNames (const ParamDef& p)
{
    const auto& f = formatFor (p);
    std::vector<std::string> out;
    if (f.list == nullptr && f.names == nullptr && ! f.onOff)
        return out;
    for (int v = p.min; v <= p.max; ++v)
        out.push_back (formatValue (p, v));
    return out;
}

const std::vector<std::string>& modSourceNames()
{
    // Order as the Peak's own menu lists them (verified on firmware 2.2, 2026-11-10).
    static const std::vector<std::string> names { "Direct", "Mod Wheel", "Aftertouch", "Expr Pedal 1", "Breath/Pedal 2", "Velocity",
                                                  "Keyboard", "LFO 1 +", "LFO 1 +/-", "LFO 2 +", "LFO 2 +/-", "Amp Env", "Mod Env 1",
                                                  "Mod Env 2", "Animate 1", "Animate 2", "CV +/-", "LFO 3 +", "LFO 3 +/-", "LFO 4 +",
                                                  "LFO 4 +/-", "Bend Wheel +", "Bend Wheel -", "Noise" };
    return names;
}

const std::vector<std::string>& modDestinationNames()
{
    static const std::vector<std::string> names { "O123 Pitch", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 3 Pitch", "Osc 1 VSync", "Osc 2 VSync",
                                             "Osc 3 VSync", "Osc 1 Shape", "Osc 2 Shape", "Osc 3 Shape", "Osc 1 Level", "Osc 2 Level",
                                             "Osc 3 Level", "Noise Level", "Ring Level", "VCA Level", "Filter Drive", "Filter Dist",
                                             "Filter Freq", "Filter Res", "LFO 1 Rate", "LFO 2 Rate", "Amp Env A", "Amp Env D", "Amp Env R",
                                             "Mod Env 1 A", "Mod Env 1 D", "Mod Env 1 R", "Mod Env 2 A", "Mod Env 2 D", "Mod Env 2 R",
                                             "FM O1>O2", "FM O2>O3", "FM O3>O1", "FM Noise>O1", "O3>Filter Freq", "Noise>Filter Freq",
                                             // 37-47: added in firmware 2.0/2.1, read from the Peak's menu (2026-11-10)
                                             "Pan Position", "O123 Shape", "Amp Env Delay", "Amp Env Hold", "Amp Env Sustain",
                                             "Mod Env 1 Delay", "Mod Env 1 Hold", "Mod Env 1 Sustain", "Mod Env 2 Delay", "Mod Env 2 Hold",
                                             "Mod Env 2 Sustain" };
    return names;
}

const std::vector<std::string>& fxSourceNames()
{
    static const std::vector<std::string> names { "Direct", "Mod Wheel", "Aftertouch", "Expr Pedal 1", "Breath/Pedal 2", "Velocity",
                                                  "Keyboard", "Animate 1", "Animate 2", "CV +/-", "LFO 3 +", "LFO 3 +/-", "LFO 4 +",
                                                  "LFO 4 +/-", "Bend Wheel +", "Bend Wheel -", "Noise" };
    return names;
}

const std::vector<std::string>& fxDestinationNames()
{
    // 0-9 from the User Guide; 10-23 read from the Peak's menu (firmware 2.2, 2026-11-10), where the
    // guide's Rev LPF/HPF became RevLdamp/RevHdamp and 2.1's destinations follow.
    static const std::vector<std::string> names { "Dist Level", "Chorus Level", "Chorus Rate", "Chorus Depth", "Chorus Feedback",
                                                  "Delay Level", "Delay Time", "Delay Feedback", "Reverb Level", "Reverb Time",
                                                  "Reverb Low Damp", "Reverb High Damp", "Reverb Size", "Reverb Low Pass",
                                                  "Reverb High Pass", "Delay Slew", "Delay Low Damp", "Delay High Damp", "Delay Width",
                                                  "Delay Ratio", "LFO 3 Rate", "LFO 4 Rate", "Noise LPF", "Noise HPF" };
    return names;
}

const std::vector<std::string>& categoryNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v;
        for (const auto* n : enumNames[0].names)
            if (n != nullptr)
                v.emplace_back (n);
        return v;
    }();
    return names;
}

int defaultValue (const ParamDef& p)
{
    const auto c = formatFor (p).centre;
    return c >= p.min && c <= p.max ? c : p.min;
}

std::string formatValue (const ParamDef& p, int v)
{
    const auto& f = formatFor (p);

    if (f.list != nullptr)
        return v >= 0 && v < (int) f.list->size() ? (*f.list)[(size_t) v] : std::to_string (v);

    if (f.names != nullptr)
    {
        const auto i = v - f.namesBase;
        if (i >= 0 && i < (int) f.names->names.size() && f.names->names[(size_t) i] != nullptr)
            return f.names->names[(size_t) i];
    }

    if (f.offAtCentre && v == f.centre)
        return "Off";
    if (f.offAtZero && v == 0)
        return "Off";
    if (f.phase)
        return v == 0 ? "Free" : std::to_string ((v - 1) * 3) + "\u00b0";
    if (f.bpm)
        return std::to_string (v) + " BPM";
    if (f.percent)
        return std::to_string (v) + "%";
    if (f.centre >= 0)
        return sign (v - f.centre);
    if (f.onOff)
        return v != 0 ? "On" : "Off";

    return std::to_string (v);
}
} // namespace peak
