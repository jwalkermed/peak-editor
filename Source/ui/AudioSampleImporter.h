#pragma once

#include "peak/core/Wavetable.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

// The Import Audio Sample window (a card in an Overlay): the sample's waveform, play and scrub through
// it, click or drag to choose where the waves are taken from (hearing the cycle there), then Import
// Waveform (one cycle into the
// selected wave) or Import Wavetable (5 successive cycles into all five waves). The waves it will take
// are shown under the timeline.
class AudioSampleImporter final : public juce::Component,
                                  private juce::Timer
{
public:
    AudioSampleImporter (const juce::String& fileName, std::shared_ptr<const juce::AudioBuffer<float>> mono, double sampleRate);
    ~AudioSampleImporter() override;

    std::function<void (std::vector<peak::Wave> waves, const juce::String& name)> onImport; // 1 wave, or 5
    std::function<void()> onClose;
    // Playback, by whoever plays audio (the editor's audio output): start or stop at a sample position,
    // and where it has got to (-1 when stopped).
    std::function<void (bool play, int position)> onPlay;
    std::function<int()> playPosition;
    // A click (or drag) on the waveform while it isn't playing, or a click on one of the five waves:
    // hear that cycle, and (with Play on the Peak on) send it to the Peak to be played there.
    std::function<void (const peak::Wave&, bool onPeak)> onAudition;
    // Play on the Peak turned off: the Peak goes back to how it was.
    std::function<void()> onPeakOff;
    // The audition's volume (0-1), shown and changed here; the editor plays and remembers it.
    void setVolume (float);
    std::function<void (float)> onVolume;

    void setStart (int sample); // the chosen location
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override; // Space plays and stops

private:
    void timerCallback() override;
    void togglePlay();
    void update(); // the waves at the chosen location
    float xFor (int sample) const;
    int sampleAt (float x) const;
    juce::String timeText (int sample) const;

    class Transport;
    juce::String name;
    std::shared_ptr<const juce::AudioBuffer<float>> audio; // mono
    double rate;
    std::vector<std::pair<float, float>> overview; // min/max per pixel column
    int start = 0, playhead = -1;
    double lastAuditionMs = 0.0;
    int chosenWave = 0; // which of the five Import Waveform takes (and was last auditioned)
    void audition();
    juce::Rectangle<int> previewBounds (int wave) const;
    std::vector<peak::Slicing::Span> spans;
    std::vector<peak::Wave> preview;

    juce::Rectangle<int> timeline, previews;
    std::unique_ptr<Transport> back, play, forward, close;
    juce::TextButton cancelButton { "Cancel" }, waveformButton, wavetableButton;
    juce::ToggleButton playOnPeak { "Play on the Peak" };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> volumeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSampleImporter)
};

// Asked on double-clicking an audio file: load it as a wavetable as it is, or open it as an audio
// sample to choose where the waves come from.
class LoadChoiceCard final : public juce::Component
{
public:
    explicit LoadChoiceCard (const juce::String& fileName);
    ~LoadChoiceCard() override { setLookAndFeel (nullptr); }
    std::function<void (bool asAudio)> onChoice;
    std::function<void()> onCancel;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String name;
    juce::TextButton asWavetable, asAudio, cancel { "Cancel" };
};
