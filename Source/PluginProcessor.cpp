#include "PluginProcessor.h"

#include "PluginEditor.h"

PeakEditorProcessor::PeakEditorProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool PeakEditorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void PeakEditorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    midi.clear(); // Phase 6: optionally forward track notes to the Peak.
}

juce::AudioProcessorEditor* PeakEditorProcessor::createEditor()
{
    return new PeakEditorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakEditorProcessor();
}
