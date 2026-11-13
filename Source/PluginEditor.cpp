#include "PluginEditor.h"

#include "peak/UpdateCheck.h"
#include "ui/HostWindow.h"
#include "ui/UpdateCard.h"
#include "ui/CardStyle.h"
#include "ui/ModAssign.h"
#include "ui/WavetableEditor.h"

#include "peak/core/ParamFormat.h"

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "ui/DrawerStyle.h"

namespace
{
constexpr int headerHeight = 44;

// A small on/off switch with a label, for the bar under the panel.
class FooterSwitch final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit FooterSwitch (const juce::String& labelText) : label (labelText) {}

    bool on = true;
    std::function<void (bool)> onChange;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto track = r.removeFromRight (26.0f).withSizeKeepingCentre (26.0f, 12.0f);
        g.setColour (on ? CardStyle::barFill.brighter (0.3f) : juce::Colour (0xff2a2c30));
        g.fillRoundedRectangle (track, 6.0f);
        g.setColour (juce::Colour (0xff3c3f44));
        g.drawRoundedRectangle (track, 6.0f, 1.0f);
        g.setColour (on ? juce::Colours::white : juce::Colours::white.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ on ? track.getRight() - 7.0f : track.getX() + 7.0f, track.getCentreY() }));
        g.setColour (juce::Colours::white.withAlpha (isMouseOver() ? 0.8f : 0.45f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (label, r.withTrimmedRight (6.0f), juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        on = ! on;
        repaint();
        if (onChange)
            onChange (on);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String label;
};

// Front panel with the playable keyboard strip underneath (it can be hidden from the bar below the panel).
class PanelPage final : public juce::Component,
                        private juce::ChangeListener
{
public:
    PanelPage (PeakModel& model, std::function<void (const juce::String&)> showTab)
        : panel (model), keys (model.getConnection()), settings (model.getConnection().getUserSettings())
    {
        panel.onShowTab = std::move (showTab);
        addAndMakeVisible (panel);
        addAndMakeVisible (keys);
        keyboardSwitch.on = settings == nullptr || settings->getBoolValue ("showKeyboard", true);
        keys.setVisible (keyboardSwitch.on);
        keyboardSwitch.setTooltip ("Show or hide the keyboard under the panel");
        keyboardSwitch.onChange = [this] (bool show) {
            keys.setVisible (show);
            if (settings != nullptr)
                settings->setValue ("showKeyboard", show);
            resized();
            repaint();
            if (onLayoutChanged)
                onLayoutChanged(); // the window's height follows
        };
        addAndMakeVisible (keyboardSwitch);
        setWantsKeyboardFocus (true);

        // A newer release on GitHub: a link beside the version, to its page.
        updateLink.setFont (juce::FontOptions (11.0f), false, juce::Justification::centredLeft);
        updateLink.setColour (juce::HyperlinkButton::textColourId, CardStyle::amber);
        addChildComponent (updateLink);
        updates->found.addChangeListener (this);
        changeListenerCallback (nullptr);
    }

    void showUpdate()
    {
        auto card = std::make_unique<UpdateCard> (updates->getNewerRelease());
        card->onClose = [this] { if (updateOverlay) updateOverlay->close(); };
        updateOverlay = std::make_unique<CardStyle::Overlay> (std::move (card), [this] { updateOverlay.reset(); });
        addAndMakeVisible (*updateOverlay);
        updateOverlay->setBounds (getLocalBounds());
        updateOverlay->grabKeyboardFocus();
    }

    ~PanelPage() override
    {
        updates->found.removeChangeListener (this);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (updates->getNewerVersion().isEmpty())
            return;
        updateLink.setButtonText ("Update available: " + updates->getNewerVersion());
        updateLink.setURL ({}); // the Update window instead of the browser
        updateLink.setTooltip ("What's new, and Install Update");
        updateLink.onClick = [this] { showUpdate(); };
        updateLink.setVisible (true);
        resized();
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
        if (updateOverlay != nullptr)
            updateOverlay->setBounds (getLocalBounds());
        auto r = getLocalBounds();
        if (keys.isVisible())
            keys.setBounds (r.removeFromBottom (KeyboardStrip::preferredHeight));
        footer = r.removeFromBottom (footerHeight);
        keyboardSwitch.setBounds (footer.reduced (12, 0).removeFromRight (90));
        updateLink.setBounds (footer.withTrimmedLeft (12 + 104).withWidth (200));
        panel.setBounds (r);
    }

    // A black bar under the panel: the editor's version at the left, the keyboard switch at the right.
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff0d0e0f));
        g.fillRect (footer);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Peak Editor v" JucePlugin_VersionString, footer.reduced (12, 0), juce::Justification::centredLeft);
    }

    FrontPanel panel;
    std::function<void()> onLayoutChanged;

    // Everything under the panel: the bar, and the keyboard if it's shown.
    int heightBelowPanel() const { return footerHeight + (keys.isVisible() ? KeyboardStrip::preferredHeight : 0); }

private:
    static constexpr int footerHeight = 18;
    KeyboardStrip keys;
    juce::PropertiesFile* settings;
    FooterSwitch keyboardSwitch { "Keyboard" };
    juce::SharedResourcePointer<UpdateCheck> updates;
    juce::HyperlinkButton updateLink;
    std::unique_ptr<CardStyle::Overlay> updateOverlay;
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
    if (! p.isStandalone())
        windowPlacer.startTimer (250);

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
    auto* modMatrix = new ModMatrixDrawer (p.getModel());
    tabs.addTab ("Mod Matrix", DrawerStyle::background, modMatrix, true);

    // Right-click modulation (panel controls and menu rows): a new slot, or an existing one, opens
    // on the Mod Matrix tab outlined, as in the TEO-5 editor.
    auto& model = p.getModel();
    ModAssign::handler().add = [&model, modMatrix, page, showTab] (int source, int destination, bool fx, int sourceB) {
        const auto& names = fx ? peak::fxSourceNames() : peak::modSourceNames();
        const auto srcName = juce::String (names[(size_t) source])
                           + (sourceB >= 0 ? juce::String::fromUTF8 (" \xc3\x97 ") + juce::String (names[(size_t) sourceB]) : juce::String());
        const auto& destName = (fx ? peak::fxDestinationNames() : peak::modDestinationNames())[(size_t) destination];
        modMatrix->userEdit ("add " + srcName + " to " + juce::String (destName));
        const auto slot = ModAssign::assign (model, source, destination, fx, sourceB);
        if (slot < 0)
            return;
        page->panel.closeCard();
        showTab ("Mod Matrix");
        modMatrix->focusSlot (fx ? 16 + slot : slot, "New in " + juce::String (fx ? "FX slot " : "slot ") + juce::String (slot + 1) + ": "
                                                       + srcName + " to " + juce::String (destName)
                                                       + ", depth 0 (no change yet). Drag the depth to set it.");
    };
    ModAssign::handler().scale = [&model, modMatrix, page, showTab] (int slot, bool fx, int source) {
        const auto label = juce::String (fx ? "FX " : "") + juce::String (slot + 1);
        modMatrix->userEdit ("source B of slot " + label);
        ModAssign::setSourceB (model, slot, fx, source);
        page->panel.closeCard();
        showTab ("Mod Matrix");
        modMatrix->focusSlot (fx ? 16 + slot : slot, ModAssign::describe (model, slot, fx) + ": "
                                                       + juce::String ((fx ? peak::fxSourceNames() : peak::modSourceNames())[(size_t) source])
                                                       + " now scales it (Source B).");
    };
    ModAssign::handler().show = [modMatrix, page, showTab] (int row, const juce::String& message) {
        page->panel.closeCard();
        showTab ("Mod Matrix");
        modMatrix->focusSlot (row, message);
    };
    tabs.addTab ("Settings", DrawerStyle::background, new SettingsDrawer (p.getModel()), true);
    tabs.addTab ("Wavetables", DrawerStyle::background, new WavetableEditor (p.getModel()), true);
    tabs.addTab ("MIDI", tabColour, new MidiMonitorPanel (p.getModel().getConnection()), true);
    tabs.setTabBarDepth (30);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
    // Resizing keeps the front panel's proportions: the window's height follows its width (and
    // the reverse when dragging the top or bottom edge), allowing for the bars above and below.
    const auto design = FrontPanel::designBounds();
    const auto fixedHeight = [this, page] {
        return headerHeight + (projectMessage.isVisible() ? bannerHeight : 0) + tabs.getTabBarDepth() + page->heightBelowPanel();
    };
    aspect.heightFor = [fixedHeight, design] (int w) { return fixedHeight() + juce::roundToInt ((float) w * design.getHeight() / design.getWidth()); };
    aspect.widthFor = [fixedHeight, design] (int h) { return juce::roundToInt ((float) (h - fixedHeight()) * design.getWidth() / design.getHeight()); };
    page->onLayoutChanged = [this] { setSize (getWidth(), aspect.heightFor (getWidth())); };
    setConstrainer (&aspect);
    setResizable (true, true);
    setResizeLimits (900, 400, 3000, 2000);
    setSize (1440, aspect.heightFor (1440));
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

void PeakEditorEditor::placeWindow()
{
    const auto position = HostWindow::topLeft (*this);
    auto* settings = peakProcessor.getModel().getConnection().getUserSettings();
    if (! position || settings == nullptr)
        return; // not shown yet
    const auto key = juce::String ("pluginWindow ") + juce::PluginHostType().getHostDescription();

    if (! placed)
    {
        // A moment after the DAW has put the window up, put it back where it was last time.
        if (++placeChecks < 2)
            return;
        placed = true;
        const auto saved = juce::StringArray::fromTokens (settings->getValue (key), ",", "");
        if (saved.size() == 2)
            if (const juce::Point<int> last { saved[0].getIntValue(), saved[1].getIntValue() }; last != *position && HostWindow::moveTo (*this, last))
            {
                savedPosition = last;
                return;
            }
        savedPosition = position;
        return;
    }
    // Then every place it's moved to is kept.
    if (position != savedPosition)
    {
        savedPosition = position;
        settings->setValue (key, juce::String (position->x) + "," + juce::String (position->y));
    }
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
    if (aspect.heightFor && getWidth() > 0) // the banner adds to the height: keep the panel's proportions
        setSize (getWidth(), aspect.heightFor (getWidth()));
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
