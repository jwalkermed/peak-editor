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
    std::array<const char*, 16> names;
};

// Only names confirmed by the User Guide, the firmware addendum or the Peak itself.
const Names enumNames[] {
    // From the Peak's own category list (2026-11-09); matches the factory bank A patches.
    { "^patch_category$", { "None", "Arp", "Bass", "Bell", "Classic", "Drum", "Keyboard", "Lead", "Motion", "Pad", "Poly", "SFX", "String", "User 1", "User 2" } },
    { "^osc\\d_range$", { "16'", "8'", "4'", "2'" } }, // raw 63-66
    { "^osc\\d_wave$", { "Sine", "Triangle", "Sawtooth", "Pulse", "More" } },
    { "^osc\\d_shape_source$", { "Manual", "Mod Env 1", "LFO 1" } },
    { "^lfo\\d_wave$", { "Triangle", "Sawtooth", "Square", "S+H" } },
    { "^lfo[12]_range$", { "Low", "High", "Sync" } },
    { "^lfo\\d_fade_in_out$", { "FadeIn", "FadeOut", "GateIn", "GateOut" } },
    { "^lfo\\d_monotrig$", { "Legato", "Re-Trig" } },
    { "^filter_slope$", { "12 dB", "24 dB" } },
    { "^filter_shape$", { "LP", "BP", "HP" } },
    { "^filter_env_select$", { "Amp Env", "Mod Env 1" } },
    { "^spread_mode$", { "Diverge", "Alternate", "Diverge 2", "NoteVal" } },
    { "^chorus_mode$", { "Chorus", "Flanger", "Phlanger" } },
    { "^delay_mode$", { "Original", "CrossFed", "Dual" } },
    { "^delay_output$", { "PreDamp", "PostDamp" } },
    { "^delay_timemode$", { "Normal", "Double", "Treble", "QuadLoFi", "HexVLoFi" } },
    { "^(chorus|reverb)_type$", { "1", "2", "3" } },
};
// Display rule for one parameter, worked out once from its id.
struct Format
{
    int centre = -1;                  // bipolar: raw value shown as 0
    const Names* names = nullptr;     // enumerated names
    int namesBase = 0;                // raw value of names[0]
    bool onOff = false, offAtZero = false, offAtCentre = false, percent = false, phase = false;
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
                    f.namesBase = matches (q, "^osc\\d_range$") ? 63 : 0;
                    break;
                }
            f.offAtCentre = matches (q, "^pre_glide$");
            f.offAtZero = matches (q, "(_fixed_note|_repeats)$");
            f.phase = matches (q, "^lfo\\d_phase$");
            f.percent = matches (q, "^arp_(chance|swing)$");
            f.onOff = q.min == 0 && q.max == 1 && f.names == nullptr && ! matches (q, "^(patch|mod\\d|fxmod)");
            all.push_back (f);
        }
        return all;
    }();
    return formats[(size_t) indexOf (p)];
}
} // namespace

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
    if (f.percent)
        return std::to_string (v) + "%";
    if (f.centre >= 0)
        return sign (v - f.centre);
    if (f.onOff)
        return v != 0 ? "On" : "Off";

    return std::to_string (v);
}
} // namespace peak
