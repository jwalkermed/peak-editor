#include "PluginEditor.h"

namespace
{
constexpr int headerHeight = 44;
}

PeakEditorEditor::PeakEditorEditor (PeakEditorProcessor& p)
    : AudioProcessorEditor (p), monitor (p.getConnection())
{
    addAndMakeVisible (monitor);
    setResizable (true, true);
    setResizeLimits (820, 520, 2400, 1600);
    setSize (1040, 680);
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
    g.drawText ("Editor / Librarian  ·  MIDI monitor (Phase 1)", header.withTrimmedLeft (74), juce::Justification::centredLeft);

    g.setColour (MidiMonitorPanel::accentColour);
    g.fillRect (16, headerHeight - 2, getWidth() - 32, 2);
}

void PeakEditorEditor::resized()
{
    monitor.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}
