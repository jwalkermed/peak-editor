#include "PluginEditor.h"

namespace
{
constexpr int headerHeight = 44;

// Front panel with the playable keyboard strip underneath.
class PanelPage final : public juce::Component
{
public:
    explicit PanelPage (PeakModel& model) : panel (model), keys (model.getConnection())
    {
        addAndMakeVisible (panel);
        addAndMakeVisible (keys);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        keys.setBounds (r.removeFromBottom (KeyboardStrip::preferredHeight));
        panel.setBounds (r);
    }

private:
    FrontPanel panel;
    KeyboardStrip keys;
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
    g.drawText ("Editor / Librarian  ·  front panel (Phase 4)", header.withTrimmedLeft (74), juce::Justification::centredLeft);

    g.setColour (MidiMonitorPanel::accentColour);
    g.fillRect (16, headerHeight - 2, getWidth() - 32, 2);
}

void PeakEditorEditor::resized()
{
    tabs.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}
