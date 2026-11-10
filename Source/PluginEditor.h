#pragma once

#include "PluginProcessor.h"
#include "ui/FrontPanel.h"
#include "ui/KeyboardStrip.h"
#include "ui/LibraryPanel.h"
#include "ui/MidiMonitorPanel.h"
#include "ui/ParameterListPanel.h"

class PeakEditorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PeakEditorEditor (PeakEditorProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TooltipWindow tooltips { this, 400 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakEditorEditor)
};
