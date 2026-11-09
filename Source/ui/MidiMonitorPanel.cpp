#include "MidiMonitorPanel.h"

#include "peak/core/MidiDescribe.h"

namespace
{
const juce::String noPort = "(none)";

juce::FontOptions mono (float size = 13.0f)
{
    return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain);
}

void fillPortBox (juce::ComboBox& box, const std::vector<PeakConnection::PortInfo>& devices, const juce::String& selectedId)
{
    box.clear (juce::dontSendNotification);
    box.addItem (noPort, 1);

    for (int i = 0; i < (int) devices.size(); ++i)
    {
        box.addItem (juce::String (devices[(size_t) i].name), i + 2);
        if (selectedId == juce::String (devices[(size_t) i].id))
            box.setSelectedId (i + 2, juce::dontSendNotification);
    }

    if (box.getSelectedId() == 0)
        box.setSelectedId (1, juce::dontSendNotification);
}
} // namespace

MidiMonitorPanel::MidiMonitorPanel (PeakConnection& c) : connection (c)
{
    for (auto* l : { &inputLabel, &outputLabel, &channelLabel, &ccLabel, &ccValueLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    inputBox.onChange = [this] {
        const auto devices = PeakConnection::getInputs();
        const auto i = inputBox.getSelectedId() - 2;
        connection.openInput (juce::isPositiveAndBelow (i, (int) devices.size()) ? juce::String (devices[(size_t) i].id) : juce::String());
    };

    outputBox.onChange = [this] {
        const auto devices = PeakConnection::getOutputs();
        const auto i = outputBox.getSelectedId() - 2;
        connection.openOutput (juce::isPositiveAndBelow (i, (int) devices.size()) ? juce::String (devices[(size_t) i].id) : juce::String());
    };

    for (int ch = 1; ch <= 16; ++ch)
        channelBox.addItem (juce::String (ch), ch);
    channelBox.onChange = [this] { connection.setChannel (channelBox.getSelectedId() - 1); };

    rescanButton.onClick = [this] { connection.autoConnect(); refreshPorts(); };

    ccNumber.setRange (0, 127, 1);
    ccValue.setRange (0, 127, 1);
    ccValue.setValue (64, juce::dontSendNotification);
    sendCcButton.onClick = [this] { sendCc(); };

    sysexText.setFont (mono());
    sysexText.setTextToShowWhenEmpty ("F0 00 20 29 … F7   (raw hex — sending arbitrary SysEx can overwrite patches)", juce::Colours::grey);
    sysexText.onReturnKey = [this] { sendSysexText(); };
    sendSysexButton.onClick = [this] { sendSysexText(); };

    showRealtime.onClick = [this] { connection.logRealtime = showRealtime.getToggleState(); };
    pauseButton.onClick = [this] { if (! pauseButton.getToggleState()) refreshLog(); };
    clearButton.onClick = [this] { startTimeMs = juce::Time::getMillisecondCounterHiRes(); connection.clearLog(); detail.clear(); };
    saveButton.onClick = [this] { saveCaptured(); };
    forgetButton.onClick = [this] { connection.clearCapturedSysex(); };

    logList.setRowHeight (20);
    logList.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff121314));
    logList.setMultipleSelectionEnabled (false);

    detail.setMultiLine (true);
    detail.setReadOnly (true);
    detail.setFont (mono (12.0f));
    detail.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff121314));
    detail.setTextToShowWhenEmpty ("Select a log row to see its full bytes.", juce::Colours::grey);

    capturedLabel.setColour (juce::Label::textColourId, ledColour);

    for (auto* comp : std::initializer_list<juce::Component*> { &inputBox, &outputBox, &channelBox, &rescanButton, &ccNumber, &ccValue,
                                                                &sendCcButton, &sysexText, &sendSysexButton, &showRealtime, &pauseButton,
                                                                &clearButton, &saveButton, &forgetButton, &capturedLabel, &statusLabel,
                                                                &logList, &detail })
        addAndMakeVisible (comp);

    connection.logChanged.addChangeListener (this);
    connection.stateChanged.addChangeListener (this);
    showRealtime.setToggleState (connection.logRealtime, juce::dontSendNotification);

    refreshPorts();
    refreshLog();
}

MidiMonitorPanel::~MidiMonitorPanel()
{
    connection.logChanged.removeChangeListener (this);
    connection.stateChanged.removeChangeListener (this);
}

void MidiMonitorPanel::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);
}

void MidiMonitorPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    constexpr int rowH = 28, gap = 8;

    auto row = area.removeFromTop (rowH);
    inputLabel.setBounds (row.removeFromLeft (64));
    inputBox.setBounds (row.removeFromLeft (220).reduced (4, 0));
    outputLabel.setBounds (row.removeFromLeft (72));
    outputBox.setBounds (row.removeFromLeft (220).reduced (4, 0));
    channelLabel.setBounds (row.removeFromLeft (64));
    channelBox.setBounds (row.removeFromLeft (64).reduced (4, 0));
    rescanButton.setBounds (row.removeFromLeft (80).reduced (4, 0));
    statusLabel.setBounds (row.withTrimmedLeft (8));

    area.removeFromTop (gap);
    row = area.removeFromTop (rowH);
    ccLabel.setBounds (row.removeFromLeft (30));
    ccNumber.setBounds (row.removeFromLeft (110).reduced (4, 0));
    ccValueLabel.setBounds (row.removeFromLeft (48));
    ccValue.setBounds (row.removeFromLeft (200).reduced (4, 0));
    sendCcButton.setBounds (row.removeFromLeft (90).reduced (4, 0));
    sendSysexButton.setBounds (row.removeFromRight (110).reduced (4, 0));
    sysexText.setBounds (row.withTrimmedLeft (12).reduced (4, 0));

    area.removeFromTop (gap);
    row = area.removeFromTop (rowH);
    showRealtime.setBounds (row.removeFromLeft (170));
    pauseButton.setBounds (row.removeFromLeft (80));
    clearButton.setBounds (row.removeFromLeft (100).reduced (4, 0));
    forgetButton.setBounds (row.removeFromRight (140).reduced (4, 0));
    saveButton.setBounds (row.removeFromRight (180).reduced (4, 0));
    capturedLabel.setBounds (row.withTrimmedLeft (12));

    area.removeFromTop (gap);
    detail.setBounds (area.removeFromBottom (juce::jmax (90, area.getHeight() / 4)));
    area.removeFromBottom (gap);
    logList.setBounds (area);
}

void MidiMonitorPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &connection.stateChanged)
        refreshPorts();
    else
        refreshLog();
}

void MidiMonitorPanel::refreshPorts()
{
    fillPortBox (inputBox, PeakConnection::getInputs(), connection.getInputIdentifier());
    fillPortBox (outputBox, PeakConnection::getOutputs(), connection.getOutputIdentifier());
    channelBox.setSelectedId (connection.getChannel() + 1, juce::dontSendNotification);

    if (! connection.isInputOpen() || ! connection.isOutputOpen())
        setStatus ("Peak not connected — choose its USB ports", true);
    else
        setStatus ("Connected");
}

void MidiMonitorPanel::refreshLog()
{
    const auto captured = connection.getCapturedSysex();
    size_t capturedBytes = 0;
    for (const auto& m : captured)
        capturedBytes += m.size();
    capturedLabel.setText ("Captured SysEx: " + juce::String ((int) captured.size()) + " msg, "
                               + juce::String ((juce::int64) capturedBytes) + " bytes",
                           juce::dontSendNotification);

    if (pauseButton.getToggleState())
        return;

    rows = connection.getLog();
    logList.updateContent();
    logList.scrollToEnsureRowIsOnscreen (getNumRows() - 1);
    logList.repaint();
}

int MidiMonitorPanel::getNumRows()
{
    return (int) rows.size();
}

void MidiMonitorPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;

    const auto& e = rows[(size_t) row];

    if (selected)
        g.fillAll (accentColour.withAlpha (0.3f));

    g.setFont (mono());
    const auto seconds = (e.timeMs - startTimeMs) / 1000.0;
    g.setColour (juce::Colours::grey);
    g.drawText (juce::String (seconds, 3), 6, 0, 80, height, juce::Justification::centredRight);

    g.setColour (e.outgoing ? accentColour : ledColour);
    g.drawText (e.outgoing ? "OUT →" : "IN  ←", 96, 0, 60, height, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawText (peak::describeMidi (e.bytes.data(), e.bytes.size()), 160, 0, width - 166, height, juce::Justification::centredLeft);
}

void MidiMonitorPanel::selectedRowsChanged (int lastRowSelected)
{
    if (! juce::isPositiveAndBelow (lastRowSelected, getNumRows()))
        return;

    const auto& bytes = rows[(size_t) lastRowSelected].bytes;
    juce::String text;

    // 16 bytes per line with offsets, to make dump diffing easy.
    for (size_t i = 0; i < bytes.size(); i += 16)
        text << juce::String::toHexString ((int) i).paddedLeft ('0', 4) << "  "
             << peak::toHex (bytes.data() + i, std::min<size_t> (16, bytes.size() - i)) << "\n";

    detail.setText (juce::String ((int) bytes.size()) + " bytes\n" + text, false);
}

void MidiMonitorPanel::sendCc()
{
    const auto msg = juce::MidiMessage::controllerEvent (connection.getChannel() + 1, (int) ccNumber.getValue(), (int) ccValue.getValue());
    if (! connection.send (msg))
        setStatus ("No MIDI output selected", true);
}

void MidiMonitorPanel::sendSysexText()
{
    const auto bytes = peak::parseHex (sysexText.getText().toStdString());

    if (! bytes || bytes->size() < 2 || bytes->front() != 0xF0 || bytes->back() != 0xF7)
    {
        setStatus ("SysEx must be hex bytes starting F0 and ending F7", true);
        return;
    }

    if (! connection.sendSysex (*bytes))
        setStatus ("No MIDI output selected", true);
}

void MidiMonitorPanel::saveCaptured()
{
    const auto captured = connection.getCapturedSysex();

    if (captured.empty())
    {
        setStatus ("Nothing captured yet", true);
        return;
    }

    const auto name = "peak-capture-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".syx";
    chooser = std::make_unique<juce::FileChooser> ("Save captured SysEx",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name),
                                                   "*.syx");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, captured] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              const auto data = peak::joinSysex (captured);
                              if (file.replaceWithData (data.data(), data.size()))
                                  setStatus ("Saved " + juce::String ((int) captured.size()) + " messages to " + file.getFileName());
                              else
                                  setStatus ("Could not write " + file.getFullPathName(), true);
                          });
}

void MidiMonitorPanel::setStatus (const juce::String& text, bool isError)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? juce::Colour (0xffff6b5b) : juce::Colour (0xff8fd18f));
}
