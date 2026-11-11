#pragma once

#include "peak/core/Patch.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <array>

// The user's patch library and the editor's view of the Peak's memory, shared by
// every editor in the process (use via juce::SharedResourcePointer).
//
// On disk, under ~/Library/Application Support/Peak Editor/:
//   Library/<id>.syx + Library/index.json   one file per patch, plus banks/favourites
//   Peak Banks/bank-A.syx ...               last known contents of each Peak bank
//   Peak Banks/unsent.syx                   changes to Peak memory not written to the synth yet
//   Backups/bank-A-<timestamp>.syx          taken automatically before writing a slot
// Everything is plain .syx so the data stays usable without this app.
class PatchLibrary
{
public:
    struct Entry
    {
        juce::String id;
        juce::String bank; // which of "My Banks" it belongs to
        peak::Patch patch;
        bool favourite = false;
        juce::String origin; // e.g. "Peak A001, 2026-11-09"
        juce::Time added;
    };

    PatchLibrary(); // ~/Library/Application Support/Peak Editor
    explicit PatchLibrary (const juce::File& root); // elsewhere (tests)

    static inline const juce::String defaultBank { "My Programs" };

    const std::vector<Entry>& getEntries() const noexcept { return entries; }
    const Entry* find (const juce::String& id) const;

    // "My Banks": named, ordered groups of library patches (a bank may be empty).
    const juce::StringArray& getBanks() const noexcept { return banks; }
    std::vector<const Entry*> entriesIn (const juce::String& bank) const; // in the order they were added
    juce::String addBank (const juce::String& name); // returns the (made unique) name
    void renameBank (const juce::String& from, const juce::String& to);
    void removeBank (const juce::String& name); // its patches go to the Trash

    // Adds a patch (stored without a location) to a bank. Returns the id of the new entry,
    // or of the existing entry if an identical sound is already in that bank.
    juce::String add (peak::Patch, const juce::String& origin, const juce::String& bank, bool* wasDuplicate = nullptr);
    void moveToBank (const juce::String& id, const juce::String& bank);
    // Moves entries (keeping their relative order) into a bank, before the entry now at
    // `index` in that bank (or to its end). Used for drag-and-drop reordering.
    void moveEntries (const juce::StringArray& ids, const juce::String& bank, int index);
    // Imports every patch in a .syx file into a bank named after the file.
    int importFile (const juce::File&, int* duplicates = nullptr); // returns patches added
    bool exportFile (const juce::StringArray& ids, const juce::File&) const;
    void remove (const juce::String& id);
    void rename (const juce::String& id, const juce::String& name);
    void setCategory (const juce::String& id, int category); // Patch Category, 0-14 (stored in the patch)
    void replacePatch (const juce::String& id, const peak::Patch&); // e.g. an edited version saved over it
    void setFavourite (const juce::String& id, bool);

    // Favourites for sounds in Peak memory. Keyed by the sound (name + parameters),
    // so the star follows a patch to whichever slot it is in.
    bool isPeakFavourite (const peak::Patch&) const;
    void setPeakFavourite (const peak::Patch&, bool);

    // Peak memory: what the synth holds (from bank dumps and completed writes), with any
    // NOT SENT changes on top. peakSlot() is what the library shows: the change if there is one.
    const std::optional<peak::Patch>& peakSlot (peak::Patch::Location) const;
    const std::optional<peak::Patch>& syncedSlot (peak::Patch::Location) const; // as on the synth
    void storePeakSlot (const peak::Patch&); // received from (or written to) the Peak; clears a matching change
    int knownSlots (int bank) const;         // slots whose synth contents are known

    // NOT SENT: changes to Peak memory waiting for "Save all changes".
    void stagePeakSlot (const peak::Patch&); // a located patch
    bool isUnsent (peak::Patch::Location) const;
    int unsentCount (int bank = -1) const;   // -1: all banks
    std::vector<peak::Patch> unsentPatches() const; // in bank/slot order
    void discardUnsent (peak::Patch::Location);
    void discardAllUnsent (int bank = -1);

    // Saves the synth's contents of a bank to Backups/. Fails unless all 128 slots are known.
    juce::Result backupBank (int bank, juce::File* written = nullptr) const;

    // Undo/redo for library actions. Call checkpoint() before each user action; it records
    // the library, My Banks, favourites and NOT SENT changes. Writes to the synth are not undone.
    void checkpoint();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    juce::File getRoot() const { return root; }

    juce::ChangeBroadcaster changed; // library or Peak memory view changed

private:
    struct Snapshot
    {
        std::vector<Entry> entries;
        juce::StringArray banks, peakFavourites;
        std::array<std::array<std::optional<peak::Patch>, 128>, 4> unsent;
    };
    Snapshot snapshot() const;
    void restore (const Snapshot&);
    void saveUnsent() const;

    void saveIndex() const;
    void loadIndex();
    void savePeakBank (int bank) const;

    juce::File root, libraryDir, banksDir, backupsDir;
    std::vector<Entry> entries;
    juce::StringArray banks;
    juce::StringArray peakFavourites; // sound fingerprints
    std::array<std::array<std::optional<peak::Patch>, 128>, 4> peakBanks, unsentBanks;
    std::vector<Snapshot> undoStack, redoStack;
    static constexpr size_t maxUndo = 50;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchLibrary)
};
