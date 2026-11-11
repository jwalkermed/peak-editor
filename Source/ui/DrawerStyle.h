#pragma once

#include "peak/PeakModel.h"
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
// A slider whose double-click puts back the value `resetValue` gives at that moment (the
// patch's saved value) rather than a fixed one.
class ResetSlider final : public juce::Slider
{
public:
    using juce::Slider::Slider;
    std::function<int()> resetValue;

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (resetValue)
            setValue (resetValue(), juce::sendNotificationSync);
        else
            juce::Slider::mouseDoubleClick (e);
    }
};

// Where the editor's patch was loaded from, as a Library row key: "p:<bank>:<slot>" (Peak memory,
// preferred), "l:<id>" (My Banks) or "" (nowhere). Compares the patch as loaded, so an edited
// sound still finds its home.
juce::String loadedPatchKey (const PeakModel&);
bool isEmpty (const peak::Patch&);

// Applies the drawer colours to the usual widgets.
void style (juce::TextButton&);
void style (juce::TextEditor&);
void style (juce::ToggleButton&);
} // namespace DrawerStyle
