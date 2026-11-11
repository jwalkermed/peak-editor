#include "ModAssign.h"

#include "peak/core/ParamFormat.h"

#include <map>
#include <regex>

namespace ModAssign
{
namespace
{
constexpr int depthCentre = 64; // no modulation

const peak::ParamDef& slotParam (bool fx, int slot, const char* part)
{
    // Main: mod<n>_source1/source2/depth/dest; FX: fxmod<n>_source/source2/depth/dest.
    auto id = (fx ? "fxmod" : "mod") + std::to_string (slot + 1) + "_" + part;
    if (fx && std::string (part) == "source1")
        id = "fxmod" + std::to_string (slot + 1) + "_source";
    return *peak::findParam (id);
}

int indexOf (const std::vector<std::string>& names, const std::string& name)
{
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name)
            return (int) i;
    return -1;
}

juce::String signedDepth (int raw)
{
    const auto d = raw - depthCentre;
    return d == 0 ? juce::String ("off") : (d > 0 ? "+" : "") + juce::String (d);
}

bool inUse (const PeakModel& model, bool fx, int s)
{
    return ! (model.get (slotParam (fx, s, "depth")) == depthCentre && model.get (slotParam (fx, s, "source1")) == 0
              && model.get (slotParam (fx, s, "source2")) == 0 && model.get (slotParam (fx, s, "dest")) == 0);
}
} // namespace

Handler& handler()
{
    static Handler h;
    return h;
}

int destinationFor (const peak::ParamDef& p)
{
    // The Peak's destination names (modDestinationNames) for the controls that drive them.
    static const std::map<std::string, std::string> names {
        { "filter_overdrive", "Filter Drive" }, { "filter_post_drive", "Filter Dist" }, { "filter_frequency", "Filter Freq" },
        { "filter_resonance", "Filter Res" },   { "lfo1_rate", "LFO 1 Rate" },          { "lfo1_sync_rate", "LFO 1 Rate" },
        { "lfo2_rate", "LFO 2 Rate" },          { "lfo2_sync_rate", "LFO 2 Rate" },     { "noise_level", "Noise Level" },
        { "ring_1_2", "Ring Level" },           { "vca_gain", "VCA Level" },            { "osc3_to_filter", "O3>Filter Freq" },
        { "pan_posn", "Pan Position" },
    };
    const std::string id = p.id;
    if (const auto it = names.find (id); it != names.end())
        return indexOf (peak::modDestinationNames(), it->second);

    std::smatch m;
    if (std::regex_match (id, m, std::regex ("osc(\\d)_(coarse|fine)")))
        return indexOf (peak::modDestinationNames(), "Osc " + m[1].str() + " Pitch");
    if (std::regex_match (id, m, std::regex ("osc(\\d)_vsync")))
        return indexOf (peak::modDestinationNames(), "Osc " + m[1].str() + " VSync");
    if (std::regex_match (id, m, std::regex ("osc(\\d)_(manual_shape|modenv1_to_shape|lfo1_to_shape)")))
        return indexOf (peak::modDestinationNames(), "Osc " + m[1].str() + " Shape");
    if (std::regex_match (id, m, std::regex ("mix_osc(\\d)")))
        return indexOf (peak::modDestinationNames(), "Osc " + m[1].str() + " Level");
    if (std::regex_match (id, m, std::regex ("(ampenv|modenv1|modenv2)_(attack|decay|release|sustain|delay|holdtime)")))
    {
        const auto env = m[1].str() == "ampenv" ? std::string ("Amp Env") : "Mod Env " + m[1].str().substr (6);
        const auto stage = m[2].str();
        const auto suffix = stage == "attack" ? " A" : stage == "decay" ? " D" : stage == "release" ? " R"
                          : stage == "sustain" ? " Sustain" : stage == "delay" ? " Delay" : " Hold";
        return indexOf (peak::modDestinationNames(), env + suffix);
    }
    return -1;
}

int fxDestinationFor (const peak::ParamDef& p)
{
    static const std::map<std::string, std::string> names {
        { "distortion_level", "Dist Level" },  { "chorus_level", "Chorus Level" },     { "chorus_rate", "Chorus Rate" },
        { "chorus_depth", "Chorus Depth" },    { "chorus_feedback", "Chorus Feedback" }, { "delay_level", "Delay Level" },
        { "delay_time", "Delay Time" },        { "delay_feedback", "Delay Feedback" },   { "reverb_level", "Reverb Level" },
        { "reverb_time", "Reverb Time" },      { "reverb_damp_lp", "Reverb Low Damp" },  { "reverb_damp_hp", "Reverb High Damp" },
        { "reverb_size", "Reverb Size" },      { "reverb_lowpass", "Reverb Low Pass" },  { "reverb_highpass", "Reverb High Pass" },
        { "delay_slew", "Delay Slew" },        { "delay_lp_damp", "Delay Low Damp" },    { "delay_hp_damp", "Delay High Damp" },
        { "delay_width", "Delay Width" },      { "delay_lr_ratio", "Delay Ratio" },      { "lfo3_rate", "LFO 3 Rate" },
        { "lfo3_ratesync", "LFO 3 Rate" },     { "lfo4_rate", "LFO 4 Rate" },            { "lfo4_ratesync", "LFO 4 Rate" },
        { "noise_lpf", "Noise LPF" },          { "noise_hpf", "Noise HPF" },
    };
    if (const auto it = names.find (p.id); it != names.end())
        return indexOf (peak::fxDestinationNames(), it->second);
    return -1;
}

juce::Array<int> sourcesFor (const peak::ParamDef& p)
{
    const juce::String id (p.id);
    juce::StringArray names;
    if (id.startsWith ("lfo1_")) names = { "LFO 1 +", "LFO 1 +/-" };
    else if (id.startsWith ("lfo2_")) names = { "LFO 2 +", "LFO 2 +/-" };
    else if (id.startsWith ("lfo3_")) names = { "LFO 3 +", "LFO 3 +/-" };
    else if (id.startsWith ("lfo4_")) names = { "LFO 4 +", "LFO 4 +/-" };
    else if (id.startsWith ("ampenv_")) names = { "Amp Env" };
    else if (id.startsWith ("modenv1_")) names = { "Mod Env 1" };
    else if (id.startsWith ("modenv2_")) names = { "Mod Env 2" };
    else if (id.startsWith ("anim1_") || id == "animate1_hold") names = { "Animate 1" };
    else if (id.startsWith ("anim2_") || id == "animate2_hold") names = { "Animate 2" };
    juce::Array<int> out;
    for (const auto& n : names)
        if (const auto i = indexOf (peak::modSourceNames(), n.toStdString()); i >= 0)
            out.add (i);
    return out;
}

int firstFreeSlot (const PeakModel& model, bool fx)
{
    for (int s = 0; s < (fx ? 4 : 16); ++s)
        if (! inUse (model, fx, s))
            return s;
    return -1;
}

juce::Array<int> slotsFor (const PeakModel& model, int destination, bool fx)
{
    juce::Array<int> found;
    for (int s = 0; s < (fx ? 4 : 16); ++s)
        if (inUse (model, fx, s) && model.get (slotParam (fx, s, "dest")) == destination)
            found.add (s);
    return found;
}

juce::Array<int> slotsScaledBy (const PeakModel& model, int source, bool fx)
{
    juce::Array<int> found;
    for (int s = 0; s < (fx ? 4 : 16); ++s)
        if (inUse (model, fx, s) && source != 0 && model.get (slotParam (fx, s, "source2")) == source)
            found.add (s);
    return found;
}

juce::Array<int> slotsFrom (const PeakModel& model, int source, bool fx)
{
    juce::Array<int> found;
    for (int s = 0; s < (fx ? 4 : 16); ++s)
        if (inUse (model, fx, s) && model.get (slotParam (fx, s, "source1")) == source)
            found.add (s);
    return found;
}

int assign (PeakModel& model, int source, int destination, bool fx, int sourceB)
{
    const auto slot = firstFreeSlot (model, fx);
    if (slot < 0)
        return -1;
    model.set (slotParam (fx, slot, "depth"), depthCentre); // none first: nothing changes until the depth is dragged
    model.set (slotParam (fx, slot, "source1"), source);
    if (sourceB >= 0)
        model.set (slotParam (fx, slot, "source2"), sourceB);
    model.set (slotParam (fx, slot, "dest"), destination);
    return slot;
}

void setSourceB (PeakModel& model, int slot, bool fx, int source)
{
    model.set (slotParam (fx, slot, "source2"), source);
}

juce::String describe (const PeakModel& model, int slot, bool fx)
{
    const auto& srcNames = fx ? peak::fxSourceNames() : peak::modSourceNames();
    const auto b = model.get (slotParam (fx, slot, "source2"));
    return (fx ? "FX " : "Slot ") + juce::String (slot + 1) + ": " + juce::String (srcNames[(size_t) model.get (slotParam (fx, slot, "source1"))])
         + (b != 0 ? juce::String::fromUTF8 (" \xc3\x97 ") + juce::String (srcNames[(size_t) b]) : juce::String())
         + juce::String::fromUTF8 (" \xe2\x86\x92 ")
         + juce::String ((fx ? peak::fxDestinationNames() : peak::modDestinationNames())[(size_t) model.get (slotParam (fx, slot, "dest"))])
         + " (" + signedDepth (model.get (slotParam (fx, slot, "depth"))) + ")";
}

void showMenu (const PeakModel& model, const peak::ParamDef& p)
{
    const auto dest = destinationFor (p), fxDest = fxDestinationFor (p);
    const auto sources = sourcesFor (p);
    if (dest < 0 && fxDest < 0 && sources.isEmpty())
        return; // nothing to modulate here

    const bool fx = dest < 0 && fxDest >= 0;
    const auto destIndex = fx ? fxDest : dest;
    const auto& srcNames = fx ? peak::fxSourceNames() : peak::modSourceNames();
    const auto& destNames = fx ? peak::fxDestinationNames() : peak::modDestinationNames();
    const auto matrix = juce::String (fx ? "FX mod matrix" : "mod matrix");

    juce::PopupMenu menu;
    menu.addSectionHeader (p.name);
    if (destIndex >= 0)
    {
        const auto destName = juce::String (destNames[(size_t) destIndex]);
        const bool full = firstFreeSlot (model, fx) < 0;
        // Each source: on its own, or scaled by a controller as Source B (the Peak multiplies them).
        static const char* const scalers[] { "Mod Wheel", "Aftertouch", "Velocity", "Expr Pedal 1", "Breath/Pedal 2", "Keyboard",
                                             "Animate 1", "Animate 2" };
        juce::PopupMenu from;
        for (int s = 0; s < (int) srcNames.size(); ++s)
        {
            const auto add = [s, destIndex, fx] (int b) { if (handler().add) handler().add (s, destIndex, fx, b); };
            if (s == 0) // Direct: a fixed amount, nothing to scale
            {
                from.addItem (srcNames[0], [add] { add (-1); });
                continue;
            }
            juce::PopupMenu how;
            how.addItem ("On its own", [add] { add (-1); });
            how.addSeparator();
            how.addSectionHeader ("Scaled by (Source B)");
            for (const auto* scaler : scalers)
                if (const auto b = indexOf (srcNames, scaler); b >= 0 && b != s)
                    how.addItem (scaler, [add, b] { add (b); });
            from.addSubMenu (srcNames[(size_t) s], how);
        }
        menu.addSubMenu ("Modulate " + destName + " from", from, ! full);

        if (const auto used = slotsFor (model, destIndex, fx); ! used.isEmpty())
        {
            menu.addSeparator();
            menu.addSectionHeader ("Already modulated by");
            for (const auto slot : used)
            {
                // Both sources when Source B isn't Direct: the Peak multiplies them (B scales A).
                const auto b = model.get (slotParam (fx, slot, "source2"));
                const auto src = juce::String (srcNames[(size_t) model.get (slotParam (fx, slot, "source1"))])
                               + (b != 0 ? juce::String::fromUTF8 (" \xc3\x97 ") + juce::String (srcNames[(size_t) b]) : juce::String());
                const auto label = (fx ? "FX " : "Slot ") + juce::String (slot + 1);
                menu.addItem (label + ": " + src + " (" + signedDepth (model.get (slotParam (fx, slot, "depth"))) + ")",
                              [slot, fx, label, src, destName] {
                                  if (handler().show)
                                      handler().show (fx ? 16 + slot : slot, label + ": " + src + " to " + destName + ". Drag the depth.");
                              });
            }
        }
        if (full)
        {
            menu.addSeparator();
            menu.addItem ("All " + juce::String (fx ? 4 : 16) + " " + matrix + " slots are in use", false, false, [] {});
        }
    }

    if (! sources.isEmpty())
    {
        // Destinations grouped as in the Peak's list.
        static const std::pair<const char*, std::pair<int, int>> groups[] {
            { "Oscillators", { 0, 12 } }, { "Mixer", { 13, 15 } }, { "Filter", { 16, 19 } }, { "LFOs", { 20, 21 } },
            { "Envelopes", { 22, 30 } },  { "FM", { 31, 34 } },    { "Filter mod", { 35, 36 } }, { "Voice", { 37, 38 } },
            { "Envelope stages", { 39, 47 } },
        };
        const bool full = firstFreeSlot (model, false) < 0;
        for (const auto source : sources)
        {
            // Where this source already goes (main and FX matrix).
            const auto srcName = juce::String (peak::modSourceNames()[(size_t) source]);
            struct Going { bool fx; int slot; bool scales; };
            juce::Array<Going> going;
            for (const auto s : slotsFrom (model, source, false))
                going.add ({ false, s, false });
            for (const auto s : slotsScaledBy (model, source, false))
                going.add ({ false, s, true });
            if (const auto fxSource = indexOf (peak::fxSourceNames(), srcName.toStdString()); fxSource >= 0)
            {
                for (const auto s : slotsFrom (model, fxSource, true))
                    going.add ({ true, s, false });
                for (const auto s : slotsScaledBy (model, fxSource, true))
                    going.add ({ true, s, true });
            }
            if (! going.isEmpty())
            {
                menu.addSeparator();
                menu.addSectionHeader (srcName + " already sending to");
                for (const auto& g : going)
                {
                    const auto isFx = g.fx;
                    const auto s = g.slot;
                    const auto label = (isFx ? "FX " : "Slot ") + juce::String (s + 1);
                    const auto to = juce::String ((isFx ? peak::fxDestinationNames() : peak::modDestinationNames())[(size_t) model.get (slotParam (isFx, s, "dest"))]);
                    const auto& names = isFx ? peak::fxSourceNames() : peak::modSourceNames();
                    const auto what = g.scales ? "scales " + to + ", with " + juce::String (names[(size_t) model.get (slotParam (isFx, s, "source1"))])
                                               : "to " + to;
                    menu.addItem (label + ": " + what + " (" + signedDepth (model.get (slotParam (isFx, s, "depth"))) + ")",
                                  [isFx, s, label, srcName, to] {
                                      if (handler().show)
                                          handler().show (isFx ? 16 + s : s, label + ": " + srcName + " to " + to + ". Drag the depth.");
                                  });
                }
            }

            juce::PopupMenu to;
            for (const auto& [group, range] : groups)
            {
                juce::PopupMenu sub;
                for (int d = range.first; d <= range.second && d < (int) peak::modDestinationNames().size(); ++d)
                    sub.addItem (peak::modDestinationNames()[(size_t) d], [source, d] { if (handler().add) handler().add (source, d, false, -1); });
                to.addSubMenu (group, sub);
            }
            menu.addSeparator();
            menu.addSubMenu ("Send " + juce::String (peak::modSourceNames()[(size_t) source]) + " to", to, ! full);

            // Make this source the Source B (scaler) of a slot already in use.
            juce::PopupMenu scale;
            const auto addScaled = [&] (bool isFx, int src) {
                for (int s = 0; s < (isFx ? 4 : 16); ++s)
                {
                    if (! inUse (model, isFx, s) || model.get (slotParam (isFx, s, "source1")) == src || model.get (slotParam (isFx, s, "source2")) == src)
                        continue;
                    const auto b = model.get (slotParam (isFx, s, "source2"));
                    const auto& names = isFx ? peak::fxSourceNames() : peak::modSourceNames();
                    scale.addItem (describe (model, s, isFx) + (b != 0 ? "   (replaces " + juce::String (names[(size_t) b]) + ")" : juce::String()),
                                   [s, isFx, src] { if (handler().scale) handler().scale (s, isFx, src); });
                }
            };
            addScaled (false, source);
            if (const auto fxSource = indexOf (peak::fxSourceNames(), srcName.toStdString()); fxSource >= 0)
                addScaled (true, fxSource);
            if (scale.getNumItems() > 0)
                menu.addSubMenu ("Scale an existing slot with " + srcName, scale);
        }
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}
} // namespace ModAssign
