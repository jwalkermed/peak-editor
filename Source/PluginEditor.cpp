#include "PluginEditor.h"

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "ui/DrawerStyle.h"

namespace
{
constexpr int headerHeight = 44;

// Front panel with the playable keyboard strip underneath.
class PanelPage final : public juce::Component
{
public:
    PanelPage (PeakModel& model, std::function<void (const juce::String&)> showTab) : panel (model), keys (model.getConnection())
    {
        panel.onShowTab = std::move (showTab);
        addAndMakeVisible (panel);
        addAndMakeVisible (keys);
        setWantsKeyboardFocus (true);
    }

    // Arrow keys (also when the on-screen keyboard has focus, which passes them up):
    // Left/Right step through patches, Up/Down through banks.
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getModifiers().isAnyModifierKeyDown())
            return false;
        if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey))
            panel.stepPatch (key.isKeyCode (juce::KeyPress::rightKey) ? 1 : -1);
        else if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::downKey))
            panel.stepBank (key.isKeyCode (juce::KeyPress::upKey) ? 1 : -1);
        else
            return false;
        return true;
    }

    void visibilityChanged() override
    {
        if (isShowing() && ! hasKeyboardFocus (true))
            grabKeyboardFocus();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        keys.setBounds (r.removeFromBottom (KeyboardStrip::preferredHeight));
        footer = r.removeFromBottom (footerHeight);
        panel.setBounds (r);
    }

    // A black bar under the panel with the editor's version at the left.
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff0d0e0f));
        g.fillRect (footer);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Peak Editor v" JucePlugin_VersionString, footer.reduced (12, 0), juce::Justification::centredLeft);
    }

    FrontPanel panel;

private:
    static constexpr int footerHeight = 18;
    KeyboardStrip keys;
    juce::Rectangle<int> footer;
};
} // namespace

namespace
{
constexpr int bannerHeight = 34;
}

PeakEditorEditor::PeakEditorEditor (PeakEditorProcessor& p)
    : AudioProcessorEditor (p), peakProcessor (p)
{
    // Plugins only: the track's MIDI plays the Peak.
    forwardMidi.setToggleState (p.forwardTrackMidi, juce::dontSendNotification);
    forwardMidi.setTooltip ("Send this track's notes, pitch bend, aftertouch and CCs to the Peak on its MIDI channel");
    forwardMidi.setColour (juce::ToggleButton::textColourId, juce::Colours::white.withAlpha (0.8f));
    forwardMidi.onClick = [this] { peakProcessor.forwardTrackMidi = forwardMidi.getToggleState(); };
    addChildComponent (forwardMidi);
    forwardMidi.setVisible (! p.isStandalone());
    sendClock.setToggleState (p.sendDawClock, juce::dontSendNotification);
    sendClock.setTooltip ("Send the DAW's tempo to the Peak as MIDI clock (Start/Stop follow the transport). "
                          "Set the Peak's clock Source to Auto, Ext-Auto or the port it arrives on.");
    sendClock.setColour (juce::ToggleButton::textColourId, juce::Colours::white.withAlpha (0.8f));
    sendClock.onClick = [this] { peakProcessor.sendDawClock = sendClock.getToggleState(); };
    addChildComponent (sendClock);
    sendClock.setVisible (! p.isStandalone());

    // The standalone app: JUCE's Options menu moves here, since the window uses the macOS title bar.
    optionsButton.setTooltip ("Audio/MIDI settings, and saving or loading the app's state");
    optionsButton.setTriggeredOnMouseDown (true);
    optionsButton.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem (1, "Audio/MIDI Settings...");
        menu.addSeparator();
        menu.addItem (2, "Save current state...");
        menu.addItem (3, "Load a saved state...");
        menu.addSeparator();
        menu.addItem (4, "Reset to default state");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton),
                            [window = juce::Component::SafePointer<juce::StandaloneFilterWindow> (findParentComponentOfClass<juce::StandaloneFilterWindow>())] (int result) {
                                // Run after the menu has gone: "Reset" rebuilds this editor.
                                juce::MessageManager::callAsync ([window, result] {
                                    if (window != nullptr && result > 0)
                                        window->handleMenuResult (result);
                                });
                            });
    };
    addChildComponent (optionsButton);
    optionsButton.setVisible (p.isStandalone());

    projectMessage.setColour (juce::Label::textColourId, juce::Colours::white);
    sendProjectPatch.setTooltip ("Replace the Peak's current sound with the project's patch (nothing is saved on the Peak)");
    sendProjectPatch.onClick = [this] { peakProcessor.acceptProjectPatch (true); };
    loadProjectPatch.onClick = [this] { peakProcessor.acceptProjectPatch (false); };
    dismissProject.onClick = [this] { peakProcessor.dismissProjectPatch(); };
    for (auto* c : std::initializer_list<juce::Component*> { &projectMessage, &sendProjectPatch, &loadProjectPatch, &dismissProject })
        addChildComponent (c);
    peakProcessor.projectPatchChanged.addChangeListener (this);

    const auto tabColour = MidiMonitorPanel::panelColour;
    const auto showTab = [this] (const juce::String& name) {
        const auto names = tabs.getTabNames();
        if (names.contains (name))
            tabs.setCurrentTabIndex (names.indexOf (name));
    };
    auto* page = new PanelPage (p.getModel(), showTab);
    auto* library = new LibraryDrawer (p.getModel());
    tabs.addTab ("Front Panel", tabColour, page, true);
    tabs.addTab ("Library", DrawerStyle::background, library, true);

    // Save, as in the TEO-5 and OB-6 editors: the Library opens as a picker, then back to the panel.
    page->panel.onSave = [showTab, library] {
        showTab ("Library");
        library->beginSave();
    };
    library->onSaveFinished = [showTab, page] (bool saved, const juce::String& what) {
        showTab ("Front Panel");
        if (saved) // after Cancel the screen just shows the patch again
            page->panel.showMessage (what);
    };
    tabs.addTab ("Mod Matrix", DrawerStyle::background, new ModMatrixDrawer (p.getModel()), true);
    tabs.addTab ("Settings", DrawerStyle::background, new SettingsDrawer (p.getModel()), true);
    tabs.addTab ("MIDI Monitor", tabColour, new MidiMonitorPanel (p.getModel().getConnection()), true);
    tabs.setTabBarDepth (30);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
    setResizable (true, true);
    setResizeLimits (900, 520, 3000, 1800);
    setSize (1440, 940);
    changeListenerCallback (nullptr);
}

void PeakEditorEditor::parentHierarchyChanged()
{
    // The standalone window gets the usual macOS title bar (close/minimise/full screen at the top
    // left) instead of JUCE's own. With a maximise button and resizing, macOS shows the green
    // button's full-screen arrows rather than a zoom "+".
    if (auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>(); window != nullptr && ! window->isUsingNativeTitleBar())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::StandaloneFilterWindow> (window)] {
            if (safe != nullptr && ! safe->isUsingNativeTitleBar())
            {
                safe->setTitleBarButtonsRequired (juce::DocumentWindow::allButtons, true);
                safe->setUsingNativeTitleBar (true);
            }
        });
}

PeakEditorEditor::~PeakEditorEditor()
{
    peakProcessor.projectPatchChanged.removeChangeListener (this);
}

void PeakEditorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto& patch = peakProcessor.getProjectPatch();
    if (patch)
        projectMessage.setText ("This project's patch is \"" + juce::String (patch->name()).trim() + "\". Send it to the Peak?", juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &projectMessage, &sendProjectPatch, &loadProjectPatch, &dismissProject })
        c->setVisible (patch.has_value());
    resized();
    repaint();
}

void PeakEditorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MidiMonitorPanel::panelColour);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 0);
    // PEAK in the panel's own lettering.
    constexpr float logoH = 18.0f;
    const auto logoW = logoH * 1177.0f / 210.0f;
    g.setColour (juce::Colours::white);
    g.fillPath (FrontPanel::peakLogo ((float) header.getX() + logoW, ((float) headerHeight - logoH) * 0.5f, logoH));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Editor / Librarian", header.withTrimmedLeft (juce::roundToInt (logoW) + 12), juce::Justification::centredLeft);

    g.setColour (MidiMonitorPanel::accentColour);
    g.fillRect (16, headerHeight - 2, getWidth() - 32, 2);

    if (projectMessage.isVisible())
    {
        g.setColour (MidiMonitorPanel::ledColour.withAlpha (0.25f));
        g.fillRect (0, headerHeight, getWidth(), bannerHeight);
    }
}

void PeakEditorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (16, 8);
    optionsButton.setBounds (header.removeFromRight (80).withSizeKeepingCentre (80, 24));
    forwardMidi.setBounds (header.removeFromRight (160));
    sendClock.setBounds (header.removeFromRight (160));

    if (projectMessage.isVisible())
    {
        auto banner = area.removeFromTop (bannerHeight).reduced (16, 4);
        dismissProject.setBounds (banner.removeFromRight (80));
        banner.removeFromRight (6);
        loadProjectPatch.setBounds (banner.removeFromRight (150));
        banner.removeFromRight (6);
        sendProjectPatch.setBounds (banner.removeFromRight (120));
        projectMessage.setBounds (banner);
    }
    tabs.setBounds (area);
}
