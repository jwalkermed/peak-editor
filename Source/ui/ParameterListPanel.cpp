#include "ParameterListPanel.h"

#include "ui/MidiMonitorPanel.h"

ParameterListPanel::ParameterListPanel (PeakModel& m) : model (m), content (std::make_unique<ParamRows> (m, [] {
                                                                  std::vector<const peak::ParamDef*> all;
                                                                  for (const auto& p : peak::allParams())
                                                                      all.push_back (&p);
                                                                  return all;
                                                              }()))
{
    patchLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    patchLabel.setColour (juce::Label::textColourId, MidiMonitorPanel::ledColour);
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));

    sendButton.setTooltip ("Replaces the Peak's current sound with this patch (SysEx). No memory slot is written.");
    sendButton.onClick = [this] {
        if (! model.sendPatchToPeak())
            hintLabel.setText ("No MIDI output — choose the Peak's port on the MIDI Monitor tab", juce::dontSendNotification);
    };
    importButton.onClick = [this] { importSyx(); };
    exportButton.onClick = [this] { exportSyx(); };

    for (auto* c : std::initializer_list<juce::Component*> { &patchLabel, &hintLabel, &sendButton, &importButton, &exportButton, &viewport })
        addAndMakeVisible (c);

    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);

    model.changed.addChangeListener (this);
    refresh();
}

ParameterListPanel::~ParameterListPanel()
{
    model.changed.removeChangeListener (this);
}

void ParameterListPanel::paint (juce::Graphics& g)
{
    g.fillAll (MidiMonitorPanel::panelColour);
}

void ParameterListPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (28);
    exportButton.setBounds (top.removeFromRight (120).reduced (4, 0));
    importButton.setBounds (top.removeFromRight (120).reduced (4, 0));
    sendButton.setBounds (top.removeFromRight (160).reduced (4, 0));
    patchLabel.setBounds (top.removeFromLeft (220));
    hintLabel.setBounds (top);

    area.removeFromTop (8);
    viewport.setBounds (area);
    content->layout (area.getWidth() - viewport.getScrollBarThickness());
}

void ParameterListPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void ParameterListPanel::refresh()
{
    patchLabel.setText (model.hasPatch() ? juce::String (model.getPatch().name()) : "(no patch)", juce::dontSendNotification);
    hintLabel.setText (model.hasPatch() ? model.getSource()
                                        : "To load the Peak's current sound: Settings → Backup page, Select = Current, press Go",
                       juce::dontSendNotification);
    content->refresh();
}

void ParameterListPanel::importSyx()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a single-patch .syx", juce::File(), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::MemoryBlock data;
        file.loadFileAsData (data);
        const auto* bytes = static_cast<const uint8_t*> (data.getData());

        for (const auto& msg : peak::splitSysex (bytes, data.getSize()))
            if (const auto p = peak::Patch::fromSysex (msg))
            {
                model.loadPatch (*p, "Imported from " + file.getFileName() + " (not sent to Peak)");
                return;
            }

        hintLabel.setText (file.getFileName() + " contains no 527-byte Peak patch dump", juce::dontSendNotification);
    });
}

void ParameterListPanel::exportSyx()
{
    const auto name = juce::File::createLegalFileName (model.getPatch().name().empty() ? "patch" : juce::String (model.getPatch().name()).trim());
    chooser = std::make_unique<juce::FileChooser> ("Export patch as .syx",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".syx"), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              const auto bytes = model.getPatch().toSysex();
                              if (! file.replaceWithData (bytes.data(), bytes.size()))
                                  hintLabel.setText ("Could not write " + file.getFullPathName(), juce::dontSendNotification);
                          });
}
