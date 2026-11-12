#pragma once

#include "peak/PeakModel.h"
#include "ui/CardStyle.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include <set>

// Left-side librarian drawer, in the style of the TEO-5 and OB-6 editors: the
// Peak's banks A-D (as read from bank dumps) beside "My Banks" (imported .syx
// files and the user's own banks). Drag between the panes to copy or write;
// double-click auditions (never saves); right-click for everything else.
// Changes to Peak memory are staged as NOT SENT; "Save all changes" writes them
// after listing what they overwrite, backing each bank up first. Undo/redo covers
// every library action (not writes already sent to the synth).
class LibraryDrawer final : public juce::Component,
                            public juce::DragAndDropContainer,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener
{
public:
    explicit LibraryDrawer (PeakModel&);
    ~LibraryDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override; // opening the Library goes to the loaded patch
    bool keyPressed (const juce::KeyPress&) override; // Cmd+Z / Shift+Cmd+Z, arrow keys

    // Opens or closes every bank in both panes.
    void setAllExpanded (bool);

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    // One line in a pane: a bank header or a patch. Keys identify patches across
    // panes and drags: "p:<bank>:<slot>" for Peak memory, "l:<id>" for the library.
    struct Row
    {
        bool isBank = false;
        bool isNew = false;             // My Banks: the empty slot after a bank's last patch (a drop target)
        int peakBank = -1, slot = -1;   // Peak pane
        juce::String bank, id;          // My Banks pane
        int index = 0;                  // position within a My Banks bank
        juce::String key() const;
    };

    static constexpr int peakPane = 0, libraryPane = 1;

    // ---- Saving (the panel's Save), as in the TEO-5 and OB-6 editors ----
    // The library becomes a picker: clicking a Peak slot or a My Banks row (or a bank's empty row)
    // chooses where the editor's patch goes, and the sound stored there goes into the Peak's edit
    // buffer so it can be heard before it is replaced. The patch's own slot is chosen to begin with.
    void beginSave();
    bool isSaving() const noexcept { return saving; }
    void confirmSave(); // writes the Peak slot (bank backed up first) or stores in My Banks; the saved patch stays in the edit buffer
    void cancelSave (bool notify = true); // your edit goes back into the edit buffer
    std::function<void (bool saved, const juce::String& what)> onSaveFinished;

private:
    class ListView;
    friend class ListView;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildRows();
    bool matches (const peak::Patch&, bool favourite) const;

    // Row helpers used by the list views.
    const std::vector<Row>& rows (int pane) const { return rowLists[(size_t) pane]; }
    std::optional<peak::Patch> patchFor (const juce::String& key) const;
    bool isCollapsed (const Row&) const;
    void toggleCollapsed (const Row&);
    void rowClicked (int pane, int row, const juce::MouseEvent&);
    void rowDoubleClicked (int pane, int row);
    // Arrow keys: up/down to the previous/next patch, left/right to the same place in the
    // previous/next bank; the patch reached is loaded into the Peak's edit buffer.
    bool navigate (int rowDelta, int bankDelta);
    // The row holding the patch the editor loaded (Peak memory first, then My Banks), or "".
    juce::String findLoadedPatch() const;
    void revealLoadedPatch();
    void setSaveTarget (const juce::String& key);
    juce::String describeTarget (bool& replacing) const;
    class SavePanel;
    void rowMenu (int pane, int row);
    void rowIconClicked (int pane, int row, int icon);
    void dropKeys (const juce::StringArray& keys, int pane, int row, bool below);
    // Category: a menu of the Peak's category names; applies to the selection in My Banks,
    // or writes a single Peak slot back (confirmed, bank backed up).
    void chooseCategory (const juce::String& key);
    void applyCategory (const juce::String& key, int category);
    juce::PopupMenu categoryMenu (const juce::String& key);

    juce::StringArray selectedKeys (int pane) const;
    void audition (const juce::String& key);
    void addToBank (const juce::StringArray& keys, const juce::String& bank, int index = -1, bool checkpointFirst = true);
    void exportKeys (const juce::StringArray& keys, const juce::String& suggestedName);
    // Stages patches into consecutive Peak slots as NOT SENT changes.
    void stageForPeak (std::vector<peak::Patch> patches, peak::Patch::Location start, const juce::String& what, bool checkpointFirst = true);
    // Peak to Peak: the dragged patches and the ones at the target swap places (NOT SENT).
    void swapInPeak (const juce::StringArray& keys, peak::Patch::Location target);
    void emptyPeakSlots (const juce::StringArray& keys);
    void saveAllChanges();
    void updateToolbar();
    void storeEditorPatch (const juce::String& bank);
    void sendBankToPeak (const juce::String& bank, int peakBank);
    void renameOnPeak (const peak::Patch&);
    void copyPeakToLibrary();
    // Fetch: reads banks straight from the Peak, one slot at a time (request 0x41 per slot).
    void fetchMenu();
    // expected: patches just written, by bank * 128 + slot; the fetch then checks each arrived intact.
    void startFetch (const std::vector<int>& banks, std::map<int, peak::Patch> expected = {});
    void fetchNext();
    void stopFetch (const juce::String& why);
    // At startup: read all four banks quietly and compare with the editor's copy. Banks changed
    // outside the editor (e.g. by Components) are reported; the old copy goes to Backups/ first.
    void startSyncCheck();
    void finishSyncCheck();
    void warnOutOfSync (const juce::String& what, const juce::StringArray& backups);
    // allowEmpty: OK with an empty field still calls back.
    void askText (const juce::String& title, const juce::String& prompt, const juce::String& initial, std::function<void (const juce::String&)>, bool allowEmpty = false);
    void setStatus (const juce::String&);
    juce::String today() const;

    PeakModel& model;
    PatchLibrary& library;

    juce::Label title { {}, "LIBRARY" };
    juce::TextButton helpButton { "?" }, fetchButton { "Fetch" }, importButton { "Import" }, saveAllButton { "Save all changes" },
        newBankButton { "New Bank" }, undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton pullAllButton { juce::String::fromUTF8 ("\xe2\x86\x93 Peak \xe2\x86\x92 Library (banks A\xe2\x80\x93" "D)") },
        pushButton { juce::String::fromUTF8 ("\xe2\x86\x91 Library \xe2\x86\x92 Peak (a bank)") };
    juce::TextEditor search;
    juce::ToggleButton favouritesOnly { "Favourites" };
    juce::ComboBox categoryFilter;
    std::array<std::unique_ptr<ListView>, 2> lists;
    std::array<juce::Viewport, 2> viewports;
    std::array<juce::Label, 2> paneTitles, paneHints;
    juce::Label status;

    std::array<std::vector<Row>, 2> rowLists;
    std::set<juce::String> selection, expanded; // row keys; bank keys are "pb:<n>" / "lb:<name>"
    juce::String loadedKey;
    juce::String homeKey; // findLoadedPatch(), kept current: marked EDITED while the editor's sound differs
    bool homeEdited = false;
    bool saving = false, auditioned = false;
    juce::String saveTarget; // a row key while saving
    std::unique_ptr<SavePanel> savePanel;
    std::vector<peak::Patch::Location> fetchQueue;
    size_t fetchIndex = 0;
    int fetchRetries = 0, fetchMissed = 0;
    juce::String fetchWhat;
    std::map<int, peak::Patch> fetchExpected;
    bool syncCheck = false;
    std::array<std::vector<std::optional<peak::Patch>>, 4> syncBefore; // the editor's copy when the check began
    std::array<juce::StringArray, 4> syncChanged;
    juce::StringArray fetchMismatches;
    juce::TimedCallback fetchTimeout { [this] { fetchTimeout.stopTimer(); if (++fetchRetries > 2) { ++fetchMissed; ++fetchIndex; fetchRetries = 0; } fetchNext(); } };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<CardStyle::Overlay> helpOverlay;

public:
    void showHelp(); // the help card (the ? button)

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryDrawer)
};
