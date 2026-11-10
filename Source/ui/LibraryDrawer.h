#pragma once

#include "peak/PeakModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

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
    bool keyPressed (const juce::KeyPress&) override; // Cmd+Z / Shift+Cmd+Z

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
    void showFetchHelp();
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
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryDrawer)
};
