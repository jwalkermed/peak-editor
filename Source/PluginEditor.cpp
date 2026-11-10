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

PeakEditorEditor::PeakEditorEditor (PeakEditorProcessor& p)
    : AudioProcessorEditor (p)
{
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
    g.drawText (juce::String::fromUTF8 ("Editor / Librarian  ·  front panel (Phase 4)"), header.withTrimmedLeft (74), juce::Justification::centredLeft);

    g.setColour (MidiMonitorPanel::accentColour);
    g.fillRect (16, headerHeight - 2, getWidth() - 32, 2);
}

void PeakEditorEditor::resized()
{
    tabs.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}
