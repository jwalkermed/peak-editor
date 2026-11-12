#pragma once

#include "peak/PatchLibrary.h"
#include "peak/PeakConnection.h"
#include "peak/core/Patch.h"
#include "peak/core/Settings.h"
#include "peak/core/Wavetable.h"

#include <map>

// The editor's view of the Peak's edit buffer, shared by every editor window in
// the process (use via juce::SharedResourcePointer). Message thread only.
// Keeps itself in sync with knob moves and patch dumps coming from the Peak, and
// sends user edits as throttled CC/NRPN. Located (bank) dumps from the Peak go to
// the library's view of Peak memory; current-sound dumps load into the editor.
class PeakModel final : private juce::ChangeListener,
                        private juce::Timer
{
public:
    PeakModel();
    ~PeakModel() override;

    PeakConnection& getConnection() { return *connection; }
    PatchLibrary& getLibrary() { return *library; }
    const PatchLibrary& getLibrary() const { return *library; }

    const peak::Patch& getPatch() const noexcept { return patch; }
    int get (const peak::ParamDef&) const;

    // A user edit: updates the model and sends it to the Peak (coalesced while dragging).
    void set (const peak::ParamDef&, int value);

    // Replaces the whole patch in the editor. Sending is explicit.
    void loadPatch (const peak::Patch&, const juce::String& source);
    // Replaces the Peak's current sound with the editor's patch (SysEx, nothing saved).
    bool sendPatchToPeak();
    // loadPatch + sendPatchToPeak.
    bool audition (const peak::Patch&, const juce::String& source);

    // Compare: toggles between the edited patch and the one originally loaded.
    void toggleCompare();
    bool isComparing() const noexcept { return comparing; }
    // Revert: drops every edit, back to the patch as loaded, and sends it to the Peak.
    bool revert();
    bool isModified() const { return ! patch.sameSound (reference); }
    const peak::Patch& getReference() const noexcept { return reference; } // the patch as loaded
    // The library row the patch was loaded from ("l:<id>"), if the Library loaded it; cleared by loadPatch.
    void setLoadedFrom (const juce::String& key) { loadedFrom = key; }
    const juce::String& getLoadedFrom() const noexcept { return loadedFrom; }
    // Renames the editor's patch and sends it to the Peak's edit buffer (nothing is saved).
    void rename (const juce::String& newName);
    // A parameter's value in the patch as it was loaded (Revert / Restore go back to it).
    int getStored (const peak::ParamDef& def) const { return def.isStored() ? reference.get (def) : get (def); }

    // Overwrites a memory slot on the Peak with the editor's patch. Callers confirm
    // with the user and back up the bank first (see LibraryPanel).
    bool writeToSlot (peak::Patch::Location);
    // The editor's sound has been saved elsewhere (e.g. by Save all changes): it is the new
    // "as loaded" patch, at `where` if that is a Peak slot.
    void markSaved (std::optional<peak::Patch::Location> where, const juce::String& newSource);
    // Changes a value in the editor's patch and in its saved version alike (it has been saved
    // separately, e.g. the category from the screen), and sends it to the Peak.
    void setSavedValue (const peak::ParamDef&, int value);
    // Where the loaded patch came from on the Peak, if anywhere: the default Save target,
    // and the slot Patch -/+ step from.
    std::optional<peak::Patch::Location> getOriginalLocation() const { return originalLocation; }

    // Selects a slot on the Peak (Program Change) and loads its sound from the library's
    // copy of Peak memory; if that bank hasn't been read, only the location is shown.
    bool selectPeakSlot (peak::Patch::Location);
    // Asks the Peak for its current sound (and its settings); the replies load as they arrive.
    bool requestFromPeak();
    // Reads the 10 user wavetables' names from the Peak (they change when wavetables are loaded
    // with Components); the names update as the replies arrive.
    bool requestUserWavetableNames();

    // User wavetables (slot 0-9 = User 1-10): read one from the Peak (its 5 waves arrive, then
    // onWavetableRead), or upload one (name, then 5 waves, as Components sends them). An upload
    // plays at once but is only kept if it's saved on the Peak (Osc menu, page 9 "User Waves", Save).
    bool requestUserWavetable (int slot);
    bool sendUserWavetable (int slot, const peak::Wavetable&);
    std::function<void (int slot, const peak::Wavetable&)> onWavetableRead;
    // Called (on the message thread) each time a patch from a Peak slot arrives and is stored.
    std::function<void (peak::Patch::Location)> onSlotStored;
    // A slot the Peak selected turned out to hold something other than the editor's copy (changed
    // outside the editor, e.g. by Components). The editor's copy is updated and the old one saved
    // to Backups/ (`backup`).
    std::function<void (peak::Patch::Location, const juce::File& backup)> onSlotChangedOutside;
    // Patch -/+: the next/previous slot, across banks (D127 wraps to A000).
    bool stepPeakSlot (int delta);

    // Where the current patch came from, e.g. "Received from Peak at 14:38".
    juce::String getSource() const { return source; }
    bool hasPatch() const noexcept { return loaded; }

    // The parameter most recently changed from either side, for the panel display.
    const peak::ParamDef* getLastTouched() const noexcept { return lastTouched; }
    void setLastTouched (const peak::ParamDef* p) { lastTouched = p; changed.sendChangeMessage(); }

    // The Peak's global settings, from the last Settings dump (kept between sessions) and
    // kept current by the NRPNs the Peak sends when a setting changes. -1 = not known yet.
    // The Peak ignores incoming NRPN 64:n, so edits go back as a whole Settings dump.
    int getSetting (const peak::SettingDef&) const;
    bool hasSettings() const noexcept { return settings.has_value(); }
    void setSetting (const peak::SettingDef&, int value); // sends a Settings dump
    void setLocalControl (bool on);                        // CC 122
    juce::String getSettingsSource() const { return settingsSource; }

    juce::ChangeBroadcaster changed;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void followPeakSlot (peak::Patch::Location); // the Peak changed patch (or we told it to)
    void findEditedOrigin(); // the current sound matched no stored patch: is it an edited one?
    // The Peak doesn't announce some changes (its Initialise, for one), so every couple of seconds,
    // when nothing else is going on, the editor quietly asks for the current sound and compares.
    void checkPeakSound();
    void peakSoundChecked (const peak::Patch&);
    juce::TimedCallback checkTimer { [this] { checkPeakSound(); } };
    double checkSentMs = 0.0; // when the last check went out (0: no reply awaited)

    juce::SharedResourcePointer<PeakConnection> connection;
    juce::SharedResourcePointer<PatchLibrary> library;
    peak::Patch patch, reference, editedWhileComparing;
    std::optional<peak::SettingsDump> settings;
    juce::String settingsSource;
    juce::File settingsFile() const;
    juce::TimedCallback sendSettingsSoon { [this] {
        sendSettingsSoon.stopTimer();
        if (settings)
            connection->sendSysex (settings->bytes());
    } };
    std::optional<peak::Patch::Location> originalLocation;
    std::optional<peak::Patch::Location> awaitedSlot;
    std::optional<peak::Patch> checkingSlot; // the editor's copy of a slot being checked against the Peak
    juce::String loadedFrom;
    std::array<std::array<std::optional<peak::Wave>, peak::Wavetable::waveCount>, peak::Wavetable::slots> wavesArriving; // a slot asked for because its bank hadn't been read
    bool comparing = false;
    std::vector<int> unstoredValues; // parameters the dump doesn't hold (e.g. Animate Hold)
    std::map<int, int> pendingSends; // param index -> latest value
    juce::String source { "No patch loaded" };
    bool loaded = false;
    const peak::ParamDef* lastTouched = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakModel)
};
