#pragma once

#include "PluginProcessor.h"
#include "ui/MidiMonitorPanel.h"

class PeakEditorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PeakEditorEditor (PeakEditorProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MidiMonitorPanel monitor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakEditorEditor)
};
