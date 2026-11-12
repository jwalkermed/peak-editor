#include "KeyboardStrip.h"

#include "ui/MidiMonitorPanel.h"

namespace
{
constexpr int visibleOctaves = 5;
const juce::String noInput = "(none)";
} // namespace

KeyboardStrip::KeyboardStrip (PeakConnection& c)
    : connection (c), keyboard (c.keyboardState, juce::KeyboardComponentBase::horizontalKeyboard)
{
    keyboard.setAvailableRange (0, 127);
    keyboard.setLowestVisibleKey (lowestKey);
    keyboard.setKeyPressBaseOctave (lowestKey / 12 + 1);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, MidiMonitorPanel::ledColour);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, MidiMonitorPanel::ledColour.withAlpha (0.3f));
    keyboard.setWantsKeyboardFocus (true);
    addAndMakeVisible (keyboard);

    // Pitch wheel springs back to centre on release, like a real one.
    pitchWheel.setRange (-8192, 8191, 1);
    pitchWheel.setValue (0, juce::dontSendNotification);
    pitchWheel.setTooltip ("Pitch bend");
    pitchWheel.onValueChange = [this] {
        connection.send (juce::MidiMessage::pitchWheel (connection.getChannel() + 1, (int) pitchWheel.getValue() + 8192));
    };
    pitchWheel.onDragEnd = [this] { pitchWheel.setValue (0, juce::sendNotificationSync); };

    modWheel.setRange (0, 127, 1);
    modWheel.setTooltip ("Mod wheel (CC 1)");
    modWheel.onValueChange = [this] { sendController (1, (int) modWheel.getValue()); };

    for (auto* s : { &pitchWheel, &modWheel })
    {
        s->setColour (juce::Slider::trackColourId, MidiMonitorPanel::accentColour);
        s->setColour (juce::Slider::backgroundColourId, juce::Colour (0xff0d0d0d));
        addAndMakeVisible (s);
    }

    velocity.setRange (1, 127, 1);
    velocity.setValue (100, juce::dontSendNotification);
    velocity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
    velocity.onValueChange = [this] { keyboard.setVelocity ((float) velocity.getValue() / 127.0f, false); };
    keyboard.setVelocity (100.0f / 127.0f, false);

    octaveDown.onClick = [this] { shiftOctave (-1); };
    octaveUp.onClick = [this] { shiftOctave (1); };

    inputBox.setTooltip ("A MIDI keyboard whose notes, bend, mod wheel, aftertouch and sustain are played on the Peak");
    inputBox.onChange = [this] {
        const auto inputs = PeakConnection::getInputs();
        const auto i = inputBox.getSelectedId() - 2;
        connection.openKeyboardInput (juce::isPositiveAndBelow (i, (int) inputs.size()) ? juce::String (inputs[(size_t) i].id) : juce::String());
    };

    for (auto* l : { &octaveLabel, &velocityLabel, &inputLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
        l->setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (l);
    }
    octaveLabel.setJustificationType (juce::Justification::centred);

    for (auto* comp : std::initializer_list<juce::Component*> { &velocity, &octaveDown, &octaveUp, &inputBox })
        addAndMakeVisible (comp);

    connection.stateChanged.addChangeListener (this);
    refreshInputs();
    shiftOctave (0);
    startTimerHz (30);
}

void KeyboardStrip::timerCallback()
{
    // Shown without sending (they've been sent already); not while being dragged here.
    if (! pitchWheel.isMouseButtonDown())
        pitchWheel.setValue (connection.pitchBend.load() - 8192, juce::dontSendNotification);
    if (! modWheel.isMouseButtonDown())
        modWheel.setValue (connection.modWheel.load(), juce::dontSendNotification);
}

KeyboardStrip::~KeyboardStrip()
{
    connection.stateChanged.removeChangeListener (this);
}

void KeyboardStrip::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1b1d));
    g.setColour (MidiMonitorPanel::accentColour);
    g.fillRect (0, 0, getWidth(), 2);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Pitch", pitchWheel.getBounds().withY (getHeight() - 16).withHeight (14), juce::Justification::centred);
    g.drawText ("Mod", modWheel.getBounds().withY (getHeight() - 16).withHeight (14), juce::Justification::centred);
}

void KeyboardStrip::resized()
{
    auto area = getLocalBounds().reduced (10, 8).withTrimmedTop (2);

    auto wheels = area.removeFromLeft (70);
    wheels.removeFromBottom (12);
    pitchWheel.setBounds (wheels.removeFromLeft (32));
    wheels.removeFromLeft (6);
    modWheel.setBounds (wheels.removeFromLeft (32));
    area.removeFromLeft (10);

    auto controls = area.removeFromRight (230);
    area.removeFromRight (10);

    auto row = controls.removeFromTop (26);
    octaveDown.setBounds (row.removeFromLeft (60));
    octaveUp.setBounds (row.removeFromRight (60));
    octaveLabel.setBounds (row);
    controls.removeFromTop (6);
    row = controls.removeFromTop (24);
    velocityLabel.setBounds (row.removeFromLeft (60));
    velocity.setBounds (row);
    controls.removeFromTop (6);
    row = controls.removeFromTop (24);
    inputLabel.setBounds (row.removeFromLeft (76));
    inputBox.setBounds (row);

    keyboard.setBounds (area);
    keyboard.setKeyWidth ((float) area.getWidth() / (7.0f * visibleOctaves + 1.0f));
}

void KeyboardStrip::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshInputs();
}

void KeyboardStrip::refreshInputs()
{
    const auto selected = connection.getKeyboardInputIdentifier();
    const auto peakInput = connection.getInputIdentifier();

    inputBox.clear (juce::dontSendNotification);
    inputBox.addItem (noInput, 1);
    const auto inputs = PeakConnection::getInputs();
    for (int i = 0; i < (int) inputs.size(); ++i)
    {
        const juce::String id (inputs[(size_t) i].id);
        inputBox.addItem (juce::String (inputs[(size_t) i].name), i + 2);
        inputBox.setItemEnabled (i + 2, id != peakInput); // the Peak's own port would loop back
        if (id == selected)
            inputBox.setSelectedId (i + 2, juce::dontSendNotification);
    }
    if (inputBox.getSelectedId() == 0)
        inputBox.setSelectedId (1, juce::dontSendNotification);
}

void KeyboardStrip::shiftOctave (int delta)
{
    lowestKey = juce::jlimit (0, 127 - 12 * visibleOctaves, lowestKey + 12 * delta);
    keyboard.setLowestVisibleKey (lowestKey);
    keyboard.setKeyPressBaseOctave (lowestKey / 12 + 1);
    octaveLabel.setText ("C" + juce::String (lowestKey / 12 - 2) + juce::String::fromUTF8 (" – C") + juce::String (lowestKey / 12 - 2 + visibleOctaves),
                         juce::dontSendNotification);
}

void KeyboardStrip::sendController (int cc, int value)
{
    connection.send (juce::MidiMessage::controllerEvent (connection.getChannel() + 1, cc, value));
}
