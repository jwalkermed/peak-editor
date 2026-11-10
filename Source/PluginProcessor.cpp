#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "peak/core/ParamFormat.h"

namespace
{
constexpr double settleAfterLoadMs = 2000.0;
constexpr int flushHz = 30;
} // namespace

PeakEditorProcessor::PeakEditorProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    connection = &model->getConnection();

    const auto& all = peak::allParams();
    fromHost = std::make_unique<std::atomic<int>[]> (all.size());
    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto& def = all[i];
        fromHost[i] = -1;
        auto attributes = juce::AudioParameterIntAttributes().withStringFromValueFunction ([&def] (int v, int) { return juce::String (peak::formatValue (def, v)); });
        auto* p = new juce::AudioParameterInt (juce::ParameterID { def.id, 1 }, juce::String (def.section) + ": " + def.name, def.min, def.max,
                                               juce::jlimit (def.min, def.max, model->get (def)), attributes);
        p->addListener (this);
        addParameter (p);
        params.push_back (p);
    }

    ignoreHostChangesFor (settleAfterLoadMs);
    model->changed.addChangeListener (this);
    startTimerHz (flushHz);
}

PeakEditorProcessor::~PeakEditorProcessor()
{
    model->changed.removeChangeListener (this);
    for (auto* p : params)
        p->removeListener (this);
}

bool PeakEditorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void PeakEditorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();

    // Forward the track's channel messages to the Peak, each scheduled at its position in
    // the block. The standalone app's MIDI input is handled by the keyboard strip instead.
    if (forwardTrackMidi && ! isStandalone())
        for (const auto metadata : midi)
        {
            const auto* data = metadata.data;
            if (metadata.numBytes <= 3 && data[0] >= 0x80 && data[0] < 0xF0)
                connection->sendPerformance (data, (size_t) metadata.numBytes, 1000.0 * metadata.samplePosition / currentSampleRate);
        }

    midi.clear();
}

//==============================================================================
void PeakEditorProcessor::parameterValueChanged (int index, float)
{
    // May arrive on the audio thread (automation) or the message thread (our own updates).
    if (updatingFromModel || ! juce::isPositiveAndBelow (index, (int) params.size()))
        return;
    if (juce::Time::getMillisecondCounter() < acceptHostChangesAfter.load())
        return;
    fromHost[(size_t) index] = params[(size_t) index]->get();
}

void PeakEditorProcessor::timerCallback()
{
    // Host automation reaches the Peak through the model (which throttles and sends CC/NRPN).
    const auto& all = peak::allParams();
    for (size_t i = 0; i < params.size(); ++i)
        if (const auto v = fromHost[i].exchange (-1); v >= 0)
            model->set (all[i], v);
}

void PeakEditorProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Knob moves from the editor or the Peak, and loaded patches, show up in the host.
    const juce::ScopedValueSetter<bool> svs (updatingFromModel, true);
    const auto& all = peak::allParams();
    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto v = juce::jlimit (all[i].min, all[i].max, model->get (all[i]));
        if (params[i]->get() != v)
            params[i]->setValueNotifyingHost (params[i]->convertTo0to1 ((float) v));
    }
}

void PeakEditorProcessor::ignoreHostChangesFor (double ms)
{
    acceptHostChangesAfter = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
}

//==============================================================================
void PeakEditorProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree state ("PeakEditor");
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("forwardTrackMidi", forwardTrackMidi.load(), nullptr);
    if (model->hasPatch())
    {
        const auto bytes = model->getPatch().toSysex();
        state.setProperty ("patch", juce::Base64::toBase64 (bytes.data(), bytes.size()), nullptr);
        state.setProperty ("patchName", juce::String (model->getPatch().name()), nullptr);
    }
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void PeakEditorProcessor::setStateInformation (const void* data, int size)
{
    ignoreHostChangesFor (settleAfterLoadMs); // the host may follow this with saved parameter values

    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    const auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType ("PeakEditor"))
        return;

    forwardTrackMidi = (bool) state.getProperty ("forwardTrackMidi", true);

    juce::MemoryOutputStream decoded;
    if (isStandalone() || ! juce::Base64::convertFromBase64 (decoded, state.getProperty ("patch").toString()))
        return;
    const auto* bytes = static_cast<const uint8_t*> (decoded.getData());
    const auto patch = peak::Patch::fromSysex (peak::Bytes (bytes, bytes + decoded.getDataSize()));

    // Offer it only if it differs from what the editor already has.
    if (patch && (! model->hasPatch() || patch->body() != model->getPatch().body()))
    {
        projectPatch = patch;
        projectPatchChanged.sendChangeMessage();
    }
}

void PeakEditorProcessor::acceptProjectPatch (bool sendToPeak)
{
    if (! projectPatch)
        return;
    const auto source = "From the project: " + juce::String (projectPatch->name());
    if (sendToPeak)
        model->audition (*projectPatch, source);
    else
        model->loadPatch (*projectPatch, source + " (not sent)");
    dismissProjectPatch();
}

void PeakEditorProcessor::dismissProjectPatch()
{
    projectPatch.reset();
    projectPatchChanged.sendChangeMessage();
}

juce::AudioProcessorEditor* PeakEditorProcessor::createEditor()
{
    return new PeakEditorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakEditorProcessor();
}
