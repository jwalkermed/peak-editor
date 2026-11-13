#pragma once

#include "PluginProcessor.h"
#include "ui/FrontPanel.h"
#include "ui/KeyboardStrip.h"
#include "ui/LibraryDrawer.h"
#include "ui/ModMatrixDrawer.h"
#include "ui/SettingsDrawer.h"
#include "ui/MidiMonitorPanel.h"

// Keeps the window in the front panel's proportions while it's dragged: dragging a side or corner
// sets the height from the width; dragging only the top or bottom sets the width from the height.
class PanelAspect final : public juce::ComponentBoundsConstrainer
{
public:
    std::function<int (int)> heightFor, widthFor;

    void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous, const juce::Rectangle<int>& limits,
                      bool top, bool left, bool bottom, bool right) override
    {
        if (heightFor && widthFor)
        {
            if (left || right || ! (top || bottom))
                bounds.setHeight (heightFor (bounds.getWidth()));
            else
                bounds.setWidth (widthFor (bounds.getHeight()));
            if (top)
                bounds.setY (previous.getBottom() - bounds.getHeight()); // the bottom edge stays put
            if (left)
                bounds.setX (previous.getRight() - bounds.getWidth());
        }
        juce::ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, top, left, bottom, right);
        if (heightFor) // the width limits may have moved it: keep the height in step
            bounds.setHeight (juce::jlimit (getMinimumHeight(), getMaximumHeight(), heightFor (bounds.getWidth())));
    }
};

class PeakEditorEditor final : public juce::AudioProcessorEditor,
                               private juce::ChangeListener
{
public:
    explicit PeakEditorEditor (PeakEditorProcessor&);
    ~PeakEditorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override; // project patch offered/cleared

    PeakEditorProcessor& peakProcessor;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    PanelAspect aspect;
    juce::ToggleButton forwardMidi { juce::String::fromUTF8 ("Track MIDI \xe2\x86\x92 Peak") };
    juce::TextButton optionsButton { "Options" }; // standalone: audio/MIDI settings and saved states
    void parentHierarchyChanged() override;
    juce::ToggleButton sendClock { juce::String::fromUTF8 ("DAW clock \xe2\x86\x92 Peak") };

    // Shown when the DAW project holds a patch that differs from the editor's.
    juce::Label projectMessage;
    juce::TextButton sendProjectPatch { "Send to Peak" }, loadProjectPatch { "Load in editor only" }, dismissProject { "Ignore" };
    juce::TooltipWindow tooltips { this, 400 };

    // A plugin's window comes back where it was last time in that DAW (the DAW places it otherwise).
    void placeWindow();
    juce::TimedCallback windowPlacer { [this] { placeWindow(); } };
    int placeChecks = 0;
    bool placed = false;
    std::optional<juce::Point<int>> savedPosition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakEditorEditor)
};
