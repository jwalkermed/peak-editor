#pragma once

#include "peak/core/Patch.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Drawer look shared with the TEO-5 and OB-6 editors: near-black drawer, striped
// 22-px rows, Exo 2 lettering, a blue pinstripe (the Peak's section-rule blue here)
// and the Peak's amber where those editors use their LED colour.
namespace DrawerStyle
{
inline const juce::Colour background { 0xff181a1c }, field { 0xff141517 }, button { 0xff232427 };
inline const juce::Colour bankRow { 0xff101113 }, rowA { 0xff202124 }, rowB { 0xff1c1d20 };
inline const juce::Colour pinstripe { 0xff62a6dc }, silkscreen { 0xffe6e6e6 }, accent { 0xffffa63c }, star { 0xffffc040 };

constexpr int rowHeight = 22;

// Exo 2 Bold, the drawer/heading face used by the sibling editors (SIL OFL, Resources/Fonts).
juce::Font font (float height);

// Rounded tab drawn on a walnut cheek, its label reading bottom-to-top.
void drawCheekTab (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, bool open);

// The Peak's Init Patch (Initialise, IniPatch mode; captured from firmware 2.2). The Peak
// can't empty a slot, so an "empty" slot is one holding this patch.
const peak::Patch& emptyPatch();
// A parameter's value in the Init Patch: what double-click resets to.
int initValue (const peak::ParamDef&);
bool isEmpty (const peak::Patch&);

// Applies the drawer colours to the usual widgets.
void style (juce::TextButton&);
void style (juce::TextEditor&);
void style (juce::ToggleButton&);
} // namespace DrawerStyle
