#pragma once

#include "peak/PeakModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Right-click modulation, as in the TEO-5 editor: on a control that is a mod destination,
// "Modulate <destination> from" a source (a new slot, depth none) or jump to the slots already
// modulating it; on a control that belongs to a source (LFO 1/2, the envelopes, Animate 1/2),
// "Send <source> to" a destination. The Mod Matrix tab then opens on that slot.
namespace ModAssign
{
// What the editor does with a choice: add (source, destination) to the main or FX matrix, or show a
// slot (0-15 main, 16-19 FX) with a message for the hint line.
struct Handler
{
    // A new slot: source (and, if >= 0, a Source B that scales it) to destination.
    std::function<void (int source, int destination, bool fx, int sourceB)> add;
    // An existing slot (0-15 main, 0-3 FX) gets `source` as its Source B.
    std::function<void (int slot, bool fx, int source)> scale;
    std::function<void (int row, const juce::String& message)> show;
};
Handler& handler(); // set by the editor

// The main-matrix destination (index into modDestinationNames) a parameter is, or -1.
int destinationFor (const peak::ParamDef&);
// The FX-matrix destination (index into fxDestinationNames) a parameter is, or -1.
int fxDestinationFor (const peak::ParamDef&);
// The main-matrix sources (indexes into modSourceNames) a parameter belongs to, e.g. LFO 1 + and +/-.
juce::Array<int> sourcesFor (const peak::ParamDef&);

// Slots: 0-15 main (mod1-16), 0-3 FX (fxmod1-4). A free slot is still as the Init Patch leaves it.
int firstFreeSlot (const PeakModel&, bool fx);
// Slots in use (not as the Init Patch leaves them, so a slot at depth 0 or switched off counts)
// that target `destination`, or that have `source` as Source A (it modulates) or Source B (it scales
// Source A: the Peak multiplies the two, e.g. LFO 1 x Mod Wheel).
juce::Array<int> slotsFor (const PeakModel&, int destination, bool fx);
juce::Array<int> slotsFrom (const PeakModel&, int source, bool fx);
juce::Array<int> slotsScaledBy (const PeakModel&, int source, bool fx);
// Fills the first free slot with source (x sourceB, if >= 0) -> destination at depth none. Returns the slot, or -1.
int assign (PeakModel&, int source, int destination, bool fx, int sourceB = -1);
// Sets a slot's Source B.
void setSourceB (PeakModel&, int slot, bool fx, int source);
// "Slot 3: Mod Wheel x LFO 1 +/- -> Filter Freq (+30)": a slot's sources, destination and depth.
juce::String describe (const PeakModel&, int slot, bool fx);

// The menu for a parameter (nothing if it can't be modulated and isn't a source).
void showMenu (const PeakModel&, const peak::ParamDef&);
} // namespace ModAssign
