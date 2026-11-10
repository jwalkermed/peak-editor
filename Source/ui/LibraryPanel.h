#pragma once

#include "peak/PeakModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Librarian: the user's patch library (left) and the Peak's memory as last read
// from bank dumps (right). Auditioning never saves; writing a slot always goes
// through SlotWriter (confirmation + automatic bank backup).
class LibraryPanel final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit LibraryPanel (PeakModel&);
    ~LibraryPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class LibraryTable;
    class SlotList;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void setStatus (const juce::String&);

    std::vector<const PatchLibrary::Entry*> visibleEntries() const;
    const PatchLibrary::Entry* selectedEntry() const;
    juce::StringArray selectedIds() const;
    peak::Patch::Location selectedSlot() const;

    void importFiles();
    void exportSelected();
    void editText (const juce::String& title, const juce::String& label, const juce::String& initial, std::function<void (const juce::String&)> apply);
    void writeSelectedTo (bool fromLibrary);
    void addBankToLibrary();

    PeakModel& model;
    PatchLibrary& library;

    juce::Label libraryTitle { {}, "LIBRARY" }, peakTitle { {}, "PEAK MEMORY" }, status, bankInfo;
    juce::TextEditor search;
    juce::ToggleButton favouritesOnly { "Favourites only" };
    std::unique_ptr<LibraryTable> table;
    juce::TextButton importButton { "Import .syx…" }, exportButton { "Export…" }, auditionButton { "Audition" },
        renameButton { "Rename…" }, tagsButton { "Tags…" }, favouriteButton { "★ Favourite" }, deleteButton { "Delete" },
        addCurrentButton { "Add editor patch" };

    juce::TextButton bankButtons[4];
    std::unique_ptr<SlotList> slots;
    juce::TextButton slotAuditionButton { "Audition" }, slotAddButton { "Add to library" }, bankAddButton { "Add bank to library" },
        writeEditorButton { "Write editor patch here…" }, writeLibraryButton { "Write library patch here…" };
    int bank = 0;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
};
