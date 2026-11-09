#pragma once

#include "peak/PeakModel.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The plugin makes no sound: it is an instrument-slot host for the editor so it
// can live on a track. All hardware I/O goes through the shared PeakConnection.
class PeakEditorProcessor final : public juce::AudioProcessor
{
public:
    PeakEditorProcessor();

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    PeakModel& getModel() { return *model; }

private:
    juce::SharedResourcePointer<PeakModel> model;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakEditorProcessor)
};
