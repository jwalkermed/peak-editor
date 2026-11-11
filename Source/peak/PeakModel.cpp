#include "PeakModel.h"

namespace
{
constexpr int sendIntervalMs = 15;
}

PeakModel::PeakModel()
    : unstoredValues (peak::allParams().size(), 0)
{
    // The last Settings dump, so the Settings tab shows values before the next one arrives.
    juce::MemoryBlock data;
    if (settingsFile().loadFileAsData (data))
    {
        const auto* b = static_cast<const uint8_t*> (data.getData());
        settings = peak::SettingsDump::fromSysex (peak::Bytes (b, b + data.getSize()));
        if (settings)
            settingsSource = "As last read from the Peak (" + settingsFile().getLastModificationTime().toString (true, true, false) + ")";
    }

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
    loadedFrom.clear();
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

bool PeakModel::revert()
{
    comparing = false;
    patch = reference;
    lastTouched = nullptr;
    pendingSends.clear();
    changed.sendChangeMessage();
    return sendPatchToPeak();
}

juce::File PeakModel::settingsFile() const
{
    return library->getRoot().getChildFile ("Peak Banks/settings.syx");
}

int PeakModel::getSetting (const peak::SettingDef& s) const
{
    return settings ? settings->get (s) : -1;
}

void PeakModel::setSetting (const peak::SettingDef& s, int value)
{
    if (s.readOnly)
        return;
    if (! settings)
        return;
    settings->set (s, juce::jlimit (s.min, s.max, value));
    settingsFile().replaceWithData (settings->bytes().data(), settings->bytes().size());
    sendSettingsSoon.startTimer (300); // one dump once a drag settles
    changed.sendChangeMessage();
}

void PeakModel::setLocalControl (bool on)
{
    connection->send (juce::MidiMessage::controllerEvent (connection->getChannel() + 1, peak::localControlCc, on ? 127 : 0));
}

bool PeakModel::selectPeakSlot (peak::Patch::Location where)
{
    if (! connection->sendPatchSelect (where.bank, where.slot))
        return false;
    followPeakSlot (where);
    return true;
}

bool PeakModel::stepPeakSlot (int delta)
{
    const auto from = originalLocation.value_or (peak::Patch::Location { 0, 0 });
    const auto index = ((from.bank * 128 + from.slot + delta) % 512 + 512) % 512;
    return selectPeakSlot ({ index / 128, index % 128 });
}

void PeakModel::rename (const juce::String& newName)
{
    if (comparing)
        toggleCompare(); // rename the edit, not the stored version
    patch.setName (newName.toStdString());
    sendPatchToPeak();
    changed.sendChangeMessage();
}

bool PeakModel::requestFromPeak()
{
    return connection->sendSysex (peak::currentSoundRequest()) && connection->sendSysex (peak::settingsRequest(), 300.0);
}

void PeakModel::followPeakSlot (peak::Patch::Location where)
{
    // The Peak plays what is stored in the slot (not any NOT SENT change to it).
    if (const auto& stored = library->syncedSlot (where))
        loadPatch (*stored, "Peak " + juce::String (where.name()));
    else
    {
        // Not read yet: ask the Peak for that slot and load it when it arrives.
        originalLocation = where;
        awaitedSlot = where;
        connection->sendSysex (peak::slotRequest (where.bank, where.slot));
        source = "Peak " + juce::String (where.name()) + " (asking the Peak)";
        changed.sendChangeMessage();
    }
    lastTouched = nullptr; // the display shows the patch
}

void PeakModel::markSaved (std::optional<peak::Patch::Location> where, const juce::String& newSource)
{
    if (comparing)
        toggleCompare();
    reference = patch;
    if (where)
        originalLocation = where;
    source = newSource;
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
    library->discardUnsent (where); // written directly: a staged change for this slot no longer applies
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
    std::vector<std::pair<int, int>> selections;
    connection->takeIncoming (changes, dumps, selections);

    for (const auto& [bank, slot] : selections)
        followPeakSlot ({ bank, slot });

    for (const auto& c : changes)
    {
        if (c.param == nullptr) // not a patch parameter: a global setting (NRPN 64:n) changed on the Peak
        {
            if (settings && (c.nrpn >> 7) == 64)
                for (const auto& s : peak::allSettings())
                    if (s.nrpn == (c.nrpn & 0x7F))
                        settings->set (s, c.value);
            continue;
        }
        if (c.param->isStored())
            patch.set (*c.param, c.value);
        else
            unstoredValues[(size_t) peak::indexOf (*c.param)] = c.value;
        lastTouched = c.param;
    }

    for (const auto& d : dumps)
        if (const auto s = peak::SettingsDump::fromSysex (d))
        {
            settings = s;
            settingsSource = "Read from the Peak at " + juce::Time::getCurrentTime().toString (false, true, false);
            settingsFile().replaceWithData (d.data(), d.size());
            changed.sendChangeMessage();
        }
        else if (const auto p = peak::Patch::fromSysex (d))
        {
            if (p->location())
            {
                library->storePeakSlot (*p); // bank dumps fill the Peak memory view
                if (p->location() == awaitedSlot)
                {
                    awaitedSlot.reset();
                    loadPatch (*p, "Peak " + juce::String (p->location()->name()));
                }
            }
            else
            {
                loadPatch (*p, "Received from Peak at " + juce::Time::getCurrentTime().toString (false, true, false));
                // The current sound carries no slot; if it matches one exactly (unedited), show that slot.
                for (int i = 0; i < 512 && ! originalLocation; ++i)
                    if (const auto& stored = library->syncedSlot ({ i / 128, i % 128 }); stored && stored->sameSound (*p))
                        originalLocation = peak::Patch::Location { i / 128, i % 128 };
            }
        }

    if (! changes.empty())
        changed.sendChangeMessage();
}
