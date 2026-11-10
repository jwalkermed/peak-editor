#include "PluginEditor.h"

#include "ui/DrawerStyle.h"

namespace
{
constexpr int headerHeight = 44;

// A drawer tab on the walnut cheek, as in the TEO-5 and OB-6 editors.
struct DrawerTab final : juce::Component, juce::SettableTooltipClient
{
    explicit DrawerTab (juce::String t) : text (std::move (t)) {}
    void paint (juce::Graphics& g) override { DrawerStyle::drawCheekTab (g, getLocalBounds().toFloat(), text, open); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }
    juce::String text;
    std::function<void()> onClick;
    bool open = false;
};

// Front panel with the playable keyboard strip underneath, and the library drawer
// that slides out over the panel from its LIBRARY tab.
class PanelPage final : public juce::Component
{
public:
    explicit PanelPage (PeakModel& model) : panel (model), keys (model.getConnection()), library (model)
    {
        addAndMakeVisible (panel);
        addAndMakeVisible (keys);
        addChildComponent (library);
        libraryTab.setTooltip ("Library: your patches and the Peak's banks");
        libraryTab.onClick = [this] {
            library.setVisible (! library.isVisible());
            libraryTab.open = library.isVisible();
            libraryTab.repaint();
        };
        addAndMakeVisible (libraryTab);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        keys.setBounds (r.removeFromBottom (KeyboardStrip::preferredHeight));
        panel.setBounds (r);

        const auto cheek = panel.leftCheekArea().translated ((float) panel.getX(), (float) panel.getY());
        const int tabW = juce::jmax (14, juce::roundToInt (cheek.getWidth() * 0.62f)), tabH = juce::roundToInt (cheek.getHeight() * 0.24f);
        libraryTab.setBounds (juce::Rectangle<int> (tabW, tabH).withCentre (cheek.getCentre().toInt()));
        const int x = juce::roundToInt (cheek.getRight());
        library.setBounds (x, r.getY(), juce::jmin (900, juce::roundToInt ((float) getWidth() * 0.58f)), r.getHeight());
    }

private:
    FrontPanel panel;
    KeyboardStrip keys;
    LibraryDrawer library;
    DrawerTab libraryTab { "LIBRARY" };
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

    projectMessage.setColour (juce::Label::textColourId, juce::Colours::white);
    sendProjectPatch.setTooltip ("Replace the Peak's current sound with the project's patch (nothing is saved on the Peak)");
    sendProjectPatch.onClick = [this] { peakProcessor.acceptProjectPatch (true); };
    loadProjectPatch.onClick = [this] { peakProcessor.acceptProjectPatch (false); };
    dismissProject.onClick = [this] { peakProcessor.dismissProjectPatch(); };
    for (auto* c : std::initializer_list<juce::Component*> { &projectMessage, &sendProjectPatch, &loadProjectPatch, &dismissProject })
        addChildComponent (c);
    peakProcessor.projectPatchChanged.addChangeListener (this);

    const auto tabColour = MidiMonitorPanel::panelColour;
    tabs.addTab ("Front Panel", tabColour, new PanelPage (p.getModel()), true);
    tabs.addTab ("Parameters", tabColour, new ParameterListPanel (p.getModel()), true);
    tabs.addTab ("MIDI Monitor", tabColour, new MidiMonitorPanel (p.getModel().getConnection()), true);
    tabs.setTabBarDepth (30);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
    setResizable (true, true);
    setResizeLimits (900, 520, 3000, 1800);
    setSize (1440, 940);
    changeListenerCallback (nullptr);
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
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("PEAK", header, juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Editor / Librarian", header.withTrimmedLeft (74), juce::Justification::centredLeft);

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
    forwardMidi.setBounds (header.removeFromRight (160));

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
