#include "PeakModel.h"

namespace
{
constexpr int sendIntervalMs = 15;
}

PeakModel::PeakModel()
    : unstoredValues (peak::allParams().size(), 0)
{
    connection->incoming.addChangeListener (this);
}

PeakModel::~PeakModel()
{
    connection->incoming.removeChangeListener (this);
}

int PeakModel::get (const peak::ParamDef& def) const
{
    return def.isStored() ? patch.get (def) : unstoredValues[(size_t) peak::indexOf (def)];
}

void PeakModel::set (const peak::ParamDef& def, int value)
{
    value = juce::jlimit (def.min, def.max, value);
    if (value == get (def))
        return;

    if (def.isStored())
        patch.set (def, value);
    else
        unstoredValues[(size_t) peak::indexOf (def)] = value;

    pendingSends[peak::indexOf (def)] = value;
    if (! isTimerRunning())
        startTimer (sendIntervalMs);

    changed.sendChangeMessage();
}

void PeakModel::timerCallback()
{
    stopTimer();
    const auto& all = peak::allParams();
    for (const auto& [index, value] : pendingSends)
        connection->sendParam (all[(size_t) index], value);
    pendingSends.clear();
}

void PeakModel::loadPatch (const peak::Patch& newPatch, const juce::String& newSource)
{
    patch = newPatch;
    source = newSource;
    loaded = true;
    pendingSends.clear();
    changed.sendChangeMessage();
}

bool PeakModel::sendPatchToPeak()
{
    pendingSends.clear();
    return connection->sendPaced (peak::encodePatch (patch, connection->getChannel()));
}

void PeakModel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    std::vector<peak::ParamChange> changes;
    std::vector<peak::Bytes> dumps;
    connection->takeIncoming (changes, dumps);

    for (const auto& c : changes)
    {
        if (c.param->isStored())
            patch.set (*c.param, c.value);
        else
            unstoredValues[(size_t) peak::indexOf (*c.param)] = c.value;
    }

    if (! dumps.empty())
        if (const auto p = peak::Patch::fromSysex (dumps.back()))
            loadPatch (*p, "Received from Peak at " + juce::Time::getCurrentTime().toString (false, true, false));

    if (! changes.empty())
        changed.sendChangeMessage();
}
