#pragma once

#include "peak/PeakConnection.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Playable strip under the front panel: pitch and mod wheels, an on-screen
// keyboard (mouse or computer keyboard), octave/velocity, and the choice
// of a hardware keyboard input whose notes are forwarded to the Peak.
class KeyboardStrip final : public juce::Component,
                            private juce::ChangeListener
{
public:
    explicit KeyboardStrip (PeakConnection&);
    ~KeyboardStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 118;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshInputs();
    void shiftOctave (int delta);
    void sendController (int cc, int value);

    PeakConnection& connection;
    juce::MidiKeyboardComponent keyboard;
    juce::Slider pitchWheel { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Slider modWheel { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Slider velocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton octaveDown { juce::String::fromUTF8 ("Oct –") }, octaveUp { "Oct +" };
    juce::Label octaveLabel, velocityLabel { {}, "Velocity" }, inputLabel { {}, "Keyboard in" };
    juce::ComboBox inputBox;
    int lowestKey = 36; // C2 on screen

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardStrip)
};
