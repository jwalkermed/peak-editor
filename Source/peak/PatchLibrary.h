#pragma once

#include "peak/core/Patch.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <array>

// The user's patch library and the editor's view of the Peak's memory, shared by
// every editor in the process (use via juce::SharedResourcePointer).
//
// On disk, under ~/Library/Application Support/Peak Editor/:
//   Library/<id>.syx + Library/index.json   one file per patch, plus names/tags/favourites
//   Peak Banks/bank-A.syx ...               last known contents of each Peak bank
//   Backups/bank-A-<timestamp>.syx          taken automatically before writing a slot
// Everything is plain .syx so the data stays usable without this app.
class PatchLibrary
{
public:
    struct Entry
    {
        juce::String id;
        peak::Patch patch;
        juce::StringArray tags;
        bool favourite = false;
        juce::String origin; // e.g. "Peak A001, 2026-11-09"
        juce::Time added;
    };

    PatchLibrary();

    const std::vector<Entry>& getEntries() const noexcept { return entries; }
    const Entry* find (const juce::String& id) const;

    // Adds a patch (stored without a location). Returns the id of the new entry, or
    // of the existing entry if an identical sound is already in the library.
    juce::String add (peak::Patch, const juce::String& origin, bool* wasDuplicate = nullptr);
    int importFile (const juce::File&, int* duplicates = nullptr); // returns patches added
    bool exportFile (const juce::StringArray& ids, const juce::File&) const;
    void remove (const juce::String& id);
    void rename (const juce::String& id, const juce::String& name);
    void setTags (const juce::String& id, const juce::StringArray& tags);
    void setFavourite (const juce::String& id, bool);

    // Peak memory as last seen in bank dumps.
    const std::optional<peak::Patch>& peakSlot (peak::Patch::Location) const;
    void storePeakSlot (const peak::Patch&); // a located patch received from the Peak
    int knownSlots (int bank) const;

    // Saves the cached contents of a bank to Backups/. Fails unless all 128 slots are known.
    juce::Result backupBank (int bank, juce::File* written = nullptr) const;

    juce::File getRoot() const { return root; }

    juce::ChangeBroadcaster changed; // library or Peak memory view changed

private:
    void saveIndex() const;
    void loadIndex();
    void savePeakBank (int bank) const;

    juce::File root, libraryDir, banksDir, backupsDir;
    std::vector<Entry> entries;
    std::array<std::array<std::optional<peak::Patch>, 128>, 4> peakBanks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchLibrary)
};
