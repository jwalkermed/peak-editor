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

    comparing = false; // editing ends a compare, as on the hardware
    lastTouched = &def;
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
    originalLocation = newPatch.location();
    patch = newPatch;
    patch.setLocation (std::nullopt); // the editor always works on the current sound
    reference = patch;
    comparing = false;
    source = newSource;
    loaded = true;
    pendingSends.clear();
    changed.sendChangeMessage();
}

bool PeakModel::sendPatchToPeak()
{
    // A dump without a location replaces the current sound without saving (verified on hardware).
    pendingSends.clear();
    auto current = patch;
    current.setLocation (std::nullopt);
    return connection->sendSysex (current.toSysex());
}

bool PeakModel::audition (const peak::Patch& p, const juce::String& newSource)
{
    loadPatch (p, newSource);
    return sendPatchToPeak();
}

void PeakModel::toggleCompare()
{
    if (! loaded)
        return;

    if (comparing)
        patch = editedWhileComparing;
    else
    {
        editedWhileComparing = patch;
        patch = reference;
    }
    comparing = ! comparing;
    sendPatchToPeak();
    changed.sendChangeMessage();
}

bool PeakModel::writeToSlot (peak::Patch::Location where)
{
    if (comparing)
        toggleCompare(); // write the edited sound, not the reference

    auto stored = patch;
    stored.setLocation (where);
    if (! connection->sendSysex (stored.toSysex()))
        return false;

    library->storePeakSlot (stored);
    reference = patch;
    originalLocation = where;
    source = "Saved to " + juce::String (where.name());
    changed.sendChangeMessage();
    return true;
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
        lastTouched = c.param;
    }

    for (const auto& d : dumps)
        if (const auto p = peak::Patch::fromSysex (d))
        {
            if (p->location())
                library->storePeakSlot (*p); // bank dumps fill the Peak memory view
            else
                loadPatch (*p, "Received from Peak at " + juce::Time::getCurrentTime().toString (false, true, false));
        }

    if (! changes.empty())
        changed.sendChangeMessage();
}
