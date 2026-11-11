#pragma once

#include "peak/PeakModel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

// The plugin makes no sound: it puts the editor on a DAW track. All hardware I/O
// goes through the shared PeakConnection. It also
//  - exposes every Peak parameter for host automation (sent on to the Peak as CC/NRPN),
//  - forwards the track's MIDI to the Peak (sample-accurate, on the Peak's channel),
//  - saves the current patch in the project and offers to restore it on load.
class PeakEditorProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorParameter::Listener,
                                  private juce::ChangeListener,
                                  private juce::Timer
{
public:
    PeakEditorProcessor();
    ~PeakEditorProcessor() override;

    void prepareToPlay (double sampleRate, int) override { currentSampleRate = sampleRate; }
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

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    PeakModel& getModel() { return *model; }
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }

    // Track MIDI -> Peak (plugins only; the standalone app uses the keyboard strip).
    std::atomic<bool> forwardTrackMidi { true };

    // A patch saved in the DAW project that differs from what the editor has. The editor
    // asks before sending it; nothing is sent automatically.
    const std::optional<peak::Patch>& getProjectPatch() const { return projectPatch; }
    void acceptProjectPatch (bool sendToPeak);
    void dismissProjectPatch();
    juce::ChangeBroadcaster projectPatchChanged;

private:
    void parameterValueChanged (int index, float) override;
    void parameterGestureChanged (int, bool) override {}
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void ignoreHostChangesFor (double ms);

    juce::SharedResourcePointer<PeakModel> model;
    PeakConnection* connection = nullptr;
    std::vector<juce::AudioParameterInt*> params; // same order as peak::allParams()
    std::unique_ptr<std::atomic<int>[]> fromHost;  // pending host values, -1 = none
    bool updatingFromModel = false;
    // Hosts may push saved parameter values when a project loads; those must not reach the
    // Peak unasked, so host changes are ignored until this time (ms counter).
    std::atomic<juce::uint32> acceptHostChangesAfter { 0 };
    double currentSampleRate = 44100.0;

    std::optional<peak::Patch> projectPatch;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PeakEditorProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakEditorProcessor)
};
