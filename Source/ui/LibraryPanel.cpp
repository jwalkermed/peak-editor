#include "LibraryPanel.h"

#include "ui/MidiMonitorPanel.h"
#include "ui/SlotWriter.h"

namespace
{
enum Column { favouriteCol = 1, nameCol, categoryCol, tagsCol, originCol };

juce::String patchName (const peak::Patch& p)
{
    const auto n = juce::String (p.name()).trim();
    return n.isEmpty() ? juce::String ("(unnamed)") : n;
}

// Category/genre are shown as numbers until their names are confirmed on the Peak.
juce::String categoryText (const peak::Patch& p)
{
    return juce::String (p.get (*peak::findParam ("patch_category")));
}

void styleTitle (juce::Label& l)
{
    l.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, MidiMonitorPanel::accentColour);
}
} // namespace

//==============================================================================
class LibraryPanel::LibraryTable final : public juce::TableListBox,
                                         private juce::TableListBoxModel
{
public:
    explicit LibraryTable (LibraryPanel& o) : owner (o)
    {
        setModel (this);
        auto& h = getHeader();
        h.addColumn ("★", favouriteCol, 28, 28, 28, juce::TableHeaderComponent::notResizableOrSortable);
        h.addColumn ("Name", nameCol, 170);
        h.addColumn ("Cat", categoryCol, 40);
        h.addColumn ("Tags", tagsCol, 150);
        h.addColumn ("From", originCol, 190);
        h.setSortColumnId (nameCol, true);
        setMultipleSelectionEnabled (true);
        setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff121314));
        setRowHeight (22);
    }

    std::vector<const PatchLibrary::Entry*> rows;

    void update()
    {
        rows = owner.visibleEntries();
        const auto col = getHeader().getSortColumnId();
        const auto fwd = getHeader().isSortedForwards();
        std::stable_sort (rows.begin(), rows.end(), [&] (auto* a, auto* b) {
            const auto key = [col] (const PatchLibrary::Entry* e) {
                switch (col)
                {
                    case categoryCol: return juce::String (categoryText (e->patch)).paddedLeft ('0', 3);
                    case tagsCol: return e->tags.joinIntoString (", ");
                    case originCol: return e->origin;
                    default: return patchName (e->patch).toLowerCase();
                }
            };
            return fwd ? key (a) < key (b) : key (b) < key (a);
        });
        updateContent();
        repaint();
    }

private:
    int getNumRows() override { return (int) rows.size(); }

    void paintRowBackground (juce::Graphics& g, int, int, int, bool selected) override
    {
        if (selected)
            g.fillAll (MidiMonitorPanel::accentColour.withAlpha (0.3f));
    }

    void paintCell (juce::Graphics& g, int row, int col, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return;
        const auto& e = *rows[(size_t) row];
        juce::String text;
        switch (col)
        {
            case favouriteCol: text = e.favourite ? "★" : ""; break;
            case nameCol: text = patchName (e.patch); break;
            case categoryCol: text = categoryText (e.patch); break;
            case tagsCol: text = e.tags.joinIntoString (", "); break;
            case originCol: text = e.origin; break;
            default: break;
        }
        g.setColour (col == favouriteCol ? MidiMonitorPanel::ledColour : juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (13.5f));
        g.drawText (text, 4, 0, width - 6, height, juce::Justification::centredLeft, true);
    }

    void sortOrderChanged (int, bool) override { update(); }
    void cellDoubleClicked (int, int, const juce::MouseEvent&) override { owner.auditionButton.triggerClick(); }

    LibraryPanel& owner;
};

//==============================================================================
class LibraryPanel::SlotList final : public juce::ListBox,
                                     private juce::ListBoxModel
{
public:
    explicit SlotList (LibraryPanel& o) : owner (o)
    {
        setModel (this);
        setRowHeight (20);
        setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff121314));
    }

private:
    int getNumRows() override { return 128; }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (selected)
            g.fillAll (MidiMonitorPanel::accentColour.withAlpha (0.3f));

        const peak::Patch::Location loc { owner.bank, row };
        const auto& p = owner.library.peakSlot (loc);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
        g.setColour (juce::Colours::grey);
        g.drawText (juce::String (loc.name()), 6, 0, 50, height, juce::Justification::centredLeft);
        g.setColour (p ? juce::Colours::white.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.3f));
        g.drawText (p ? patchName (*p) : juce::String ("— not read yet"), 60, 0, width - 64, height, juce::Justification::centredLeft);
    }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.slotAuditionButton.triggerClick(); }

    LibraryPanel& owner;
};

//==============================================================================
LibraryPanel::LibraryPanel (PeakModel& m)
    : model (m), library (m.getLibrary()), table (std::make_unique<LibraryTable> (*this)), slots (std::make_unique<SlotList> (*this))
{
    styleTitle (libraryTitle);
    styleTitle (peakTitle);
    for (auto* l : { &status, &bankInfo })
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));

    search.setTextToShowWhenEmpty ("Search name, tag or origin", juce::Colours::grey);
    search.onTextChange = [this] { table->update(); };
    favouritesOnly.onClick = [this] { table->update(); };

    importButton.onClick = [this] { importFiles(); };
    exportButton.onClick = [this] { exportSelected(); };
    auditionButton.setTooltip ("Load into the editor and the Peak's current sound. Nothing is saved.");
    auditionButton.onClick = [this] {
        if (const auto* e = selectedEntry())
        {
            model.audition (e->patch, "Library: " + patchName (e->patch));
            setStatus ("Auditioning \"" + patchName (e->patch) + "\" (not saved)");
        }
    };
    renameButton.onClick = [this] {
        if (const auto* e = selectedEntry())
            editText ("Rename patch", "Name (16 characters)", patchName (e->patch), [this, id = e->id] (const juce::String& s) { library.rename (id, s.substring (0, 16)); });
    };
    tagsButton.onClick = [this] {
        if (const auto* e = selectedEntry())
            editText ("Tags", "Comma-separated tags", e->tags.joinIntoString (", "), [this, id = e->id] (const juce::String& s) {
                auto tags = juce::StringArray::fromTokens (s, ",", "");
                tags.trim();
                tags.removeEmptyStrings();
                library.setTags (id, tags);
            });
    };
    favouriteButton.onClick = [this] {
        for (const auto& id : selectedIds())
            if (const auto* e = library.find (id))
                library.setFavourite (id, ! e->favourite);
    };
    deleteButton.onClick = [this] {
        const auto ids = selectedIds();
        if (ids.isEmpty())
            return;
        const auto options = juce::MessageBoxOptions()
                                 .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                 .withTitle ("Delete from library")
                                 .withMessage ("Move " + juce::String (ids.size()) + " patch(es) to the Trash?")
                                 .withButton (juce::MessageBoxButtonOptions ("Delete", 1).withRole (juce::MessageBoxButtonRole::destroy))
                                 .withButton (juce::MessageBoxButtonOptions ("Cancel", 0).withRole (juce::MessageBoxButtonRole::cancel))
                                 .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (options, [this, ids] (int r) {
            if (r == 1)
                for (const auto& id : ids)
                    library.remove (id);
        });
    };
    addCurrentButton.onClick = [this] {
        bool dup = false;
        library.add (model.getPatch(), "Editor, " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d"), &dup);
        setStatus (dup ? "Already in the library" : "Added \"" + patchName (model.getPatch()) + "\"");
    };

    for (int b = 0; b < 4; ++b)
    {
        bankButtons[b].setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + b)));
        bankButtons[b].setClickingTogglesState (true);
        bankButtons[b].setRadioGroupId (4711);
        bankButtons[b].setColour (juce::TextButton::buttonOnColourId, MidiMonitorPanel::ledColour.withAlpha (0.8f));
        bankButtons[b].onClick = [this, b] { bank = b; refresh(); };
        addAndMakeVisible (bankButtons[b]);
    }
    bankButtons[0].setToggleState (true, juce::dontSendNotification);

    slotAuditionButton.setTooltip ("Load this slot into the editor and the Peak's current sound. Nothing is saved.");
    slotAuditionButton.onClick = [this] {
        if (const auto& p = library.peakSlot (selectedSlot()))
        {
            model.audition (*p, "Peak " + juce::String (selectedSlot().name()));
            setStatus ("Auditioning " + juce::String (selectedSlot().name()));
        }
    };
    slotAddButton.onClick = [this] {
        if (const auto& p = library.peakSlot (selectedSlot()))
        {
            bool dup = false;
            library.add (*p, "Peak " + juce::String (selectedSlot().name()) + ", " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d"), &dup);
            setStatus (dup ? "Already in the library" : "Added \"" + patchName (*p) + "\"");
        }
    };
    bankAddButton.onClick = [this] { addBankToLibrary(); };
    writeEditorButton.setTooltip ("Overwrite the selected Peak slot with the patch in the editor (asks first, backs up the bank)");
    writeEditorButton.onClick = [this] { writeSelectedTo (false); };
    writeLibraryButton.setTooltip ("Overwrite the selected Peak slot with the selected library patch (asks first, backs up the bank)");
    writeLibraryButton.onClick = [this] { writeSelectedTo (true); };

    for (auto* c : std::initializer_list<juce::Component*> { &libraryTitle, &peakTitle, &status, &bankInfo, &search, &favouritesOnly, table.get(),
                                                             &importButton, &exportButton, &auditionButton, &renameButton, &tagsButton,
                                                             &favouriteButton, &deleteButton, &addCurrentButton, slots.get(), &slotAuditionButton,
                                                             &slotAddButton, &bankAddButton, &writeEditorButton, &writeLibraryButton })
        addAndMakeVisible (c);

    library.changed.addChangeListener (this);
    refresh();
}

LibraryPanel::~LibraryPanel()
{
    library.changed.removeChangeListener (this);
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (MidiMonitorPanel::panelColour);
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    status.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (4);

    auto right = area.removeFromRight (juce::jmax (330, area.getWidth() * 2 / 5));
    area.removeFromRight (16);

    // Library
    auto top = area.removeFromTop (26);
    libraryTitle.setBounds (top.removeFromLeft (90));
    favouritesOnly.setBounds (top.removeFromRight (130));
    search.setBounds (top.reduced (4, 0));
    area.removeFromTop (6);
    auto buttons = area.removeFromBottom (28);
    area.removeFromBottom (6);
    table->setBounds (area);
    const auto bw = buttons.getWidth() / 8;
    for (auto* b : { &importButton, &exportButton, &auditionButton, &renameButton, &tagsButton, &favouriteButton, &deleteButton, &addCurrentButton })
        b->setBounds (buttons.removeFromLeft (bw).reduced (2, 0));

    // Peak memory
    top = right.removeFromTop (26);
    peakTitle.setBounds (top.removeFromLeft (120));
    for (auto& b : bankButtons)
        b.setBounds (top.removeFromLeft (36).reduced (2, 0));
    right.removeFromTop (4);
    bankInfo.setBounds (right.removeFromTop (36));
    auto rows = right.removeFromBottom (62);
    right.removeFromBottom (6);
    slots->setBounds (right);
    auto row1 = rows.removeFromTop (28);
    const auto w1 = row1.getWidth() / 3;
    slotAuditionButton.setBounds (row1.removeFromLeft (w1).reduced (2, 0));
    slotAddButton.setBounds (row1.removeFromLeft (w1).reduced (2, 0));
    bankAddButton.setBounds (row1.reduced (2, 0));
    rows.removeFromTop (6);
    writeEditorButton.setBounds (rows.removeFromLeft (rows.getWidth() / 2).reduced (2, 0));
    writeLibraryButton.setBounds (rows.reduced (2, 0));
}

void LibraryPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void LibraryPanel::refresh()
{
    table->update();
    slots->updateContent();
    slots->repaint();

    const auto known = library.knownSlots (bank);
    const juce::String letter = juce::String::charToString ((juce::juce_wchar) ('A' + bank));
    bankInfo.setText (known == 128 ? "Bank " + letter + ": all 128 slots read. Writes back the bank up first."
                                   : "Bank " + letter + ": " + juce::String (known) + "/128 read. To read it: Peak Settings > Backup, Select = Bank "
                                         + letter + ", Go.",
                      juce::dontSendNotification);
    libraryTitle.setText ("LIBRARY (" + juce::String ((int) library.getEntries().size()) + ")", juce::dontSendNotification);
}

void LibraryPanel::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
}

std::vector<const PatchLibrary::Entry*> LibraryPanel::visibleEntries() const
{
    const auto query = search.getText().trim().toLowerCase();
    std::vector<const PatchLibrary::Entry*> out;
    for (const auto& e : library.getEntries())
    {
        if (favouritesOnly.getToggleState() && ! e.favourite)
            continue;
        const auto haystack = (patchName (e.patch) + " " + e.tags.joinIntoString (" ") + " " + e.origin).toLowerCase();
        if (query.isEmpty() || haystack.contains (query))
            out.push_back (&e);
    }
    return out;
}

const PatchLibrary::Entry* LibraryPanel::selectedEntry() const
{
    const auto row = table->getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) table->rows.size()) ? table->rows[(size_t) row] : nullptr;
}

juce::StringArray LibraryPanel::selectedIds() const
{
    juce::StringArray ids;
    const auto sel = table->getSelectedRows();
    for (int i = 0; i < sel.size(); ++i)
        if (juce::isPositiveAndBelow (sel[i], (int) table->rows.size()))
            ids.add (table->rows[(size_t) sel[i]]->id);
    return ids;
}

peak::Patch::Location LibraryPanel::selectedSlot() const
{
    return { bank, juce::jmax (0, slots->getSelectedRow()) };
}

void LibraryPanel::importFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Import patches (.syx, single or bank)", juce::File(), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc) {
                              int added = 0, dups = 0;
                              for (const auto& f : fc.getResults())
                              {
                                  int d = 0;
                                  added += library.importFile (f, &d);
                                  dups += d;
                              }
                              if (! fc.getResults().isEmpty())
                                  setStatus ("Imported " + juce::String (added) + " patch(es)" + (dups > 0 ? ", skipped " + juce::String (dups) + " already in the library" : juce::String()));
                          });
}

void LibraryPanel::exportSelected()
{
    const auto ids = selectedIds();
    if (ids.isEmpty())
        return setStatus ("Select patches to export");

    const auto first = library.find (ids[0]);
    const auto name = ids.size() == 1 && first != nullptr ? juce::File::createLegalFileName (patchName (first->patch)) : juce::String ("peak-patches");
    chooser = std::make_unique<juce::FileChooser> ("Export patches", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".syx"), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, ids] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f != juce::File())
                                  setStatus (library.exportFile (ids, f) ? "Exported " + juce::String (ids.size()) + " patch(es) to " + f.getFileName()
                                                                         : "Could not write " + f.getFullPathName());
                          });
}

void LibraryPanel::editText (const juce::String& title, const juce::String& label, const juce::String& initial, std::function<void (const juce::String&)> apply)
{
    auto* w = new juce::AlertWindow (title, label, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, apply] (int r) {
                            if (r == 1)
                                apply (w->getTextEditorContents ("text"));
                        }),
                        true);
}

void LibraryPanel::writeSelectedTo (bool fromLibrary)
{
    if (slots->getSelectedRow() < 0)
        return setStatus ("Select a Peak slot to write to");

    if (fromLibrary)
    {
        const auto* e = selectedEntry();
        if (e == nullptr)
            return setStatus ("Select a library patch to write");
        // The editor holds what gets written, so the user hears exactly what will be saved.
        model.audition (e->patch, "Library: " + patchName (e->patch));
    }

    SlotWriter::confirmAndWrite (model, selectedSlot(), this, [this] (const juce::String& result) { setStatus (result); });
}

void LibraryPanel::addBankToLibrary()
{
    int added = 0, dups = 0;
    for (int s = 0; s < 128; ++s)
        if (const auto& p = library.peakSlot ({ bank, s }))
        {
            bool dup = false;
            library.add (*p, "Peak " + juce::String (p->location()->name()) + ", " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d"), &dup);
            dup ? ++dups : ++added;
        }
    setStatus ("Added " + juce::String (added) + " patch(es)" + (dups > 0 ? ", " + juce::String (dups) + " already in the library" : juce::String()));
}
