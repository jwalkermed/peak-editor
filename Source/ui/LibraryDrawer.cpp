#include "LibraryDrawer.h"

#include "peak/core/ParamFormat.h"
#include "ui/DrawerStyle.h"
#include "ui/SlotWriter.h"

#include <algorithm>
#include <limits>

namespace
{
using namespace DrawerStyle;

// Icons after the favourite star: export .syx, rename, delete (library rows only rename/delete).
enum Icon { starIcon = 0, exportIcon, renameIcon, deleteIcon, noIcon };
constexpr int starWidth = 22, iconWidth = 20, iconsRight = starWidth + 3 * iconWidth;
constexpr double msBetweenWrites = 250.0; // one 527-byte dump takes ~170 ms at DIN speed
constexpr int maxDragLines = 5;           // names shown on the drag image

int iconAt (int x)
{
    if (x < starWidth)
        return starIcon;
    if (x >= iconsRight)
        return noIcon;
    return exportIcon + (x - starWidth) / iconWidth;
}

void drawIcon (juce::Graphics& g, int icon, juce::Rectangle<float> box, juce::Colour c)
{
    const auto ctr = box.getCentre();
    juce::Path p;
    g.setColour (c);
    if (icon == exportIcon) // down arrow onto a line: to a file
    {
        p.startNewSubPath (ctr.x, ctr.y - 5.0f);
        p.lineTo (ctr.x, ctr.y + 4.0f);
        p.startNewSubPath (ctr.x - 4.0f, ctr.y + 0.5f);
        p.lineTo (ctr.x, ctr.y + 4.5f);
        p.lineTo (ctr.x + 4.0f, ctr.y + 0.5f);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRect (ctr.x - 4.5f, ctr.y + 5.5f, 9.0f, 1.2f);
    }
    else if (icon == renameIcon) // pencil
    {
        p.addRectangle (-1.6f, -6.0f, 3.2f, 9.0f);
        p.addTriangle (-1.6f, 3.4f, 1.6f, 3.4f, 0.0f, 6.0f);
        p.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4.0f).translated (ctr));
        g.strokePath (p, juce::PathStrokeType (1.1f));
    }
    else if (icon == deleteIcon) // trash can
    {
        p.addRectangle (ctr.x - 3.5f, ctr.y - 2.5f, 7.0f, 8.0f);
        p.startNewSubPath (ctr.x - 5.0f, ctr.y - 4.0f);
        p.lineTo (ctr.x + 5.0f, ctr.y - 4.0f);
        p.startNewSubPath (ctr.x - 1.5f, ctr.y - 4.0f);
        p.lineTo (ctr.x - 1.5f, ctr.y - 5.5f);
        p.lineTo (ctr.x + 1.5f, ctr.y - 5.5f);
        p.lineTo (ctr.x + 1.5f, ctr.y - 4.0f);
        p.startNewSubPath (ctr.x - 1.2f, ctr.y - 0.5f);
        p.lineTo (ctr.x - 1.2f, ctr.y + 3.5f);
        p.startNewSubPath (ctr.x + 1.2f, ctr.y - 0.5f);
        p.lineTo (ctr.x + 1.2f, ctr.y + 3.5f);
        g.strokePath (p, juce::PathStrokeType (1.1f));
    }
}

juce::String nameOf (const peak::Patch& p)
{
    const auto n = juce::String (p.name()).trim();
    return n.isEmpty() ? juce::String ("(unnamed)") : n;
}

juce::String bankLetter (int bank)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + bank));
}

juce::MessageBoxOptions confirmOptions (const juce::String& title, const juce::String& message, const juce::String& okText, juce::Component* parent)
{
    // Dismissing maps to Cancel (result 0), never to the action.
    return juce::MessageBoxOptions()
        .withIconType (juce::MessageBoxIconType::WarningIcon)
        .withTitle (title)
        .withMessage (message)
        .withButton (juce::MessageBoxButtonOptions (okText, 1).withRole (juce::MessageBoxButtonRole::destroy))
        .withButton (juce::MessageBoxButtonOptions ("Cancel", 0).withRole (juce::MessageBoxButtonRole::cancel))
        .withAssociatedComponent (parent);
}

const char* helpText =
    "PEAK (left) shows banks A-D as last read from the synth. Fetch reads them from the Peak (one bank or all four); "
    "a bank sent from the Peak's Backup page fills in here too.\n\n"
    "MY BANKS (right) is your library: imported .syx files become banks, and you can make your own.\n\n"
    "Double-click a patch to audition it: it replaces the Peak's current sound without saving.\n"
    "Drag from PEAK to MY BANKS to copy, or within MY BANKS to reorder (the Empty row at the end of a bank adds to it). "
    "Dragging within PEAK swaps patches; dragging from MY BANKS onto PEAK slots replaces them. These changes (and renaming or "
    "re-categorising PEAK patches) go into the library's copy of the Peak, marked NOT SENT. Save all changes writes them to "
    "the synth after listing what they overwrite; each bank is backed up to Application Support/Peak Editor/Backups first.\n"
    "Undo/Redo (Cmd+Z, Shift+Cmd+Z) covers every library change, but not writes already sent.\n"
    "Deleting a PEAK patch leaves the slot Empty (the Peak's Init Patch is written there when you save).\n"
    "Right-click a patch or bank for more. Search accepts cat:pad.";
} // namespace

juce::String LibraryDrawer::Row::key() const
{
    if (isNew)
        return "new:" + bank;
    if (isBank)
        return peakBank >= 0 ? "pb:" + juce::String (peakBank) : "lb:" + bank;
    return peakBank >= 0 ? "p:" + juce::String (peakBank) + ":" + juce::String (slot) : "l:" + id;
}

//==============================================================================
class LibraryDrawer::ListView final : public juce::Component,
                                      public juce::DragAndDropTarget
{
public:
    ListView (LibraryDrawer& o, int p) : owner (o), pane (p) {}

    int rowAt (int y) const { return y / rowHeight; }
    void updateSize (int width) { setSize (width, juce::jmax (1, (int) owner.rows (pane).size()) * rowHeight); }

    void paint (juce::Graphics& g) override
    {
        const auto& rows = owner.rows (pane);
        if (rows.empty())
        {
            g.setColour (silkscreen.withAlpha (0.3f));
            g.setFont (font (9.0f));
            g.drawFittedText ("Nothing matches", getLocalBounds().reduced (12), juce::Justification::centredTop, 2);
            return;
        }

        const auto clip = g.getClipBounds();
        const int first = juce::jmax (0, rowAt (clip.getY())), last = juce::jmin ((int) rows.size() - 1, rowAt (clip.getBottom()));
        for (int i = first; i <= last; ++i)
            paintRow (g, rows[(size_t) i], i);

        if (juce::isPositiveAndBelow (dropRow, (int) rows.size()))
        {
            g.setColour (accent);
            if (pane == peakPane) // replacing: outline the block of slots the patches land in
            {
                const int top = rows[(size_t) dropRow].isBank ? dropRow + 1 : dropRow;
                int bottom = top;
                while (bottom + 1 < (int) rows.size() && bottom + 1 < top + dragCount && ! rows[(size_t) bottom + 1].isBank)
                    ++bottom;
                if (top < (int) rows.size())
                    g.drawRect (juce::Rectangle<int> (1, top * rowHeight, getWidth() - 2, (bottom - top + 1) * rowHeight), 2);
            }
            else // moving/copying into My Banks: a line where it will be inserted
                g.fillRect (0, (dropRow + (dropBelow ? 1 : 0)) * rowHeight - 1, getWidth(), 3);
        }
    }

    // The category (the Peak's own tag) at the right of each row: clicking it changes it.
    bool inCategoryZone (int x) const { return x > getWidth() - rightMargin - categoryWidth && x < getWidth() - rightMargin; }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (rowAt (e.y) != hoverRow || iconAt (e.x) != iconAt (hoverX))
        {
            hoverRow = rowAt (e.y);
            hoverX = e.x;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverRow = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        pendingSelect = -1;
        owner.grabKeyboardFocus(); // for Cmd+Z / Shift+Cmd+Z
        const int row = rowAt (e.y);
        if (! juce::isPositiveAndBelow (row, (int) owner.rows (pane).size()))
            return;
        const auto& r = owner.rows (pane)[(size_t) row];

        if (e.mods.isPopupMenu())
        {
            if (! owner.selection.count (r.key()))
                owner.rowClicked (pane, row, e);
            owner.rowMenu (pane, row);
            return;
        }
        if (r.isBank)
        {
            owner.toggleCollapsed (r);
            return;
        }
        if (owner.isSaving())
        {
            owner.setSaveTarget (r.key());
            return;
        }
        if (r.isNew)
            return;
        if (const auto icon = iconAt (e.x); icon != noIcon && ! (r.peakBank >= 0 && owner.patchFor (r.key()) && isEmpty (*owner.patchFor (r.key()))))
        {
            owner.rowIconClicked (pane, row, icon);
            return;
        }
        if (inCategoryZone (e.x) && owner.patchFor (r.key()))
        {
            owner.chooseCategory (r.key());
            return;
        }
        // Pressing on a row that is already selected keeps the whole selection, so it
        // can be dragged; it narrows to that row on release if no drag happened.
        if (owner.selection.count (r.key()) && ! e.mods.isAnyModifierKeyDown())
            pendingSelect = row;
        else
            owner.rowClicked (pane, row, e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragStarted && pendingSelect >= 0 && pendingSelect < (int) owner.rows (pane).size())
            owner.rowClicked (pane, pendingSelect, e);
        pendingSelect = -1;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStarted || e.getDistanceFromDragStart() < 5 || e.mods.isPopupMenu() || owner.isSaving())
            return;
        const int row = rowAt (e.getMouseDownY());
        if (! juce::isPositiveAndBelow (row, (int) owner.rows (pane).size()) || owner.rows (pane)[(size_t) row].isBank
            || owner.rows (pane)[(size_t) row].isNew)
            return;
        auto keys = owner.selectedKeys (pane);
        if (keys.isEmpty())
            return;
        dragStarted = true;
        juce::Array<juce::var> list;
        for (const auto& k : keys)
            list.add (k);
        juce::StringArray names;
        for (const auto& k : keys)
            if (const auto p = owner.patchFor (k))
                names.add (nameOf (*p));
        const juce::Point<int> offset (-12, -11);
        owner.startDragging (juce::var (list), this, juce::ScaledImage (dragImage (names)), false, &offset);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int row = rowAt (e.y);
        if (juce::isPositiveAndBelow (row, (int) owner.rows (pane).size()) && iconAt (e.x) == noIcon && ! owner.isSaving())
            owner.rowDoubleClicked (pane, row);
    }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.isArray(); }
    void itemDragMove (const SourceDetails& d) override
    {
        dropRow = rowAt (d.localPosition.y);
        dropBelow = (d.localPosition.y % rowHeight) > rowHeight / 2;
        dragCount = d.description.isArray() ? juce::jmax (1, d.description.getArray()->size()) : 1;
        repaint();
    }
    void itemDragExit (const SourceDetails&) override
    {
        dropRow = -1;
        repaint();
    }
    void itemDropped (const SourceDetails& d) override
    {
        juce::StringArray keys;
        for (const auto& v : *d.description.getArray())
            keys.add (v.toString());
        const auto row = rowAt (d.localPosition.y);
        const auto below = (d.localPosition.y % rowHeight) > rowHeight / 2;
        dropRow = -1;
        repaint();
        owner.dropKeys (keys, pane, row, below);
    }

private:
    // What follows the mouse while dragging: the block of patches being carried, as rows.
    static juce::Image dragImage (const juce::StringArray& names)
    {
        const int lines = juce::jmin (names.size(), maxDragLines) + (names.size() > maxDragLines ? 1 : 0);
        const int w = 240, h = lines * rowHeight + 2;
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);
        g.setColour (bankRow.withAlpha (0.92f));
        g.fillRoundedRectangle (img.getBounds().toFloat(), 4.0f);
        for (int i = 0; i < lines; ++i)
        {
            const auto row = juce::Rectangle<int> (1, 1 + i * rowHeight, w - 2, rowHeight);
            const bool more = i == maxDragLines;
            g.setColour ((i % 2 ? rowB : rowA).withAlpha (0.92f));
            g.fillRect (row);
            g.setColour (pinstripe.withAlpha (0.45f));
            g.fillRect (row);
            g.setColour (more ? silkscreen.withAlpha (0.6f) : silkscreen);
            g.setFont (juce::FontOptions (13.5f));
            g.drawText (more ? "+ " + juce::String (names.size() - maxDragLines) + " more" : names[i], row.reduced (10, 0), juce::Justification::centredLeft, true);
        }
        g.setColour (pinstripe);
        g.drawRoundedRectangle (img.getBounds().toFloat().reduced (0.5f), 4.0f, 1.2f);
        return img;
    }

    void paintRow (juce::Graphics& g, const Row& row, int i)
    {
        auto r = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight);
        const auto& lib = owner.library;

        if (row.isBank)
        {
            g.setColour (bankRow);
            g.fillRect (r);
            g.setColour (pinstripe);
            g.fillRect (r.removeFromBottom (1));

            juce::Path tri;
            const float cx = 10.0f, cy = (float) i * rowHeight + rowHeight * 0.5f;
            if (owner.isCollapsed (row))
                tri.addTriangle (cx - 3, cy - 4, cx - 3, cy + 4, cx + 3, cy);
            else
                tri.addTriangle (cx - 4, cy - 3, cx + 4, cy - 3, cx, cy + 3);
            g.setColour (silkscreen.withAlpha (0.8f));
            g.fillPath (tri);

            auto text = r.withTrimmedLeft (20).withTrimmedRight (14); // clear of the scroll bar
            g.setFont (font (11.5f));
            g.setColour (silkscreen);
            g.drawText (row.peakBank >= 0 ? "Peak Bank " + bankLetter (row.peakBank) : row.bank, text, juce::Justification::centredLeft, true);

            g.setFont (font (8.5f));
            if (row.peakBank >= 0 && lib.unsentCount (row.peakBank) > 0)
            {
                g.setColour (accent);
                g.drawText (juce::String (lib.unsentCount (row.peakBank)) + " NOT SENT", text, juce::Justification::centredRight, false);
            }
            else if (row.peakBank >= 0)
            {
                const auto known = lib.knownSlots (row.peakBank);
                g.setColour (known == 0 ? accent : silkscreen.withAlpha (0.45f));
                g.drawText (known == 0 ? "NOT READ" : known == 128 ? juce::String ("128") : juce::String (known) + "/128", text, juce::Justification::centredRight, false);
            }
            else
            {
                g.setColour (silkscreen.withAlpha (0.45f));
                g.drawText (juce::String ((int) lib.entriesIn (row.bank).size()), text, juce::Justification::centredRight, false);
            }
            return;
        }

        g.setColour (i % 2 ? rowB : rowA);
        g.fillRect (r);
        const auto paintTarget = [&] {
            if (owner.isSaving() && owner.saveTarget == row.key())
            {
                g.setColour (accent.withAlpha (0.18f));
                g.fillRect (r);
                g.setColour (accent);
                g.drawRect (r, 2);
            }
        };
        if (row.isNew)
        {
            paintTarget();
            auto t = r.withTrimmedLeft (iconsRight + 16).withTrimmedRight (6);
            g.setFont (font (9.0f));
            g.setColour (silkscreen.withAlpha (0.3f));
            g.drawText (juce::String (row.index + 1).paddedLeft ('0', 2), t.removeFromLeft (36), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.0f).withStyle ("Italic"));
            g.drawText ("Empty", t, juce::Justification::centredLeft, true);
            return;
        }
        const auto key = row.key();
        if (owner.selection.count (key))
        {
            g.setColour (pinstripe.withAlpha (0.45f));
            g.fillRect (r);
        }
        paintTarget();

        const auto patch = owner.patchFor (key);
        const PatchLibrary::Entry* entry = row.peakBank < 0 ? lib.find (row.id) : nullptr;
        auto text = r.withTrimmedLeft (iconsRight + 16).withTrimmedRight (14); // clear of the scroll bar
        const auto label = row.peakBank >= 0 ? juce::String (peak::Patch::Location { row.peakBank, row.slot }.name())
                                             : juce::String (row.index + 1).paddedLeft ('0', 2);

        if (! patch)
        {
            g.setFont (font (9.0f));
            g.setColour (silkscreen.withAlpha (0.3f));
            g.drawText (label, text.removeFromLeft (36), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.0f).withStyle ("Italic"));
            g.drawText ("Not read yet", text, juce::Justification::centredLeft, true);
            return;
        }

        if (row.peakBank >= 0 && isEmpty (*patch))
        {
            // Holding the Init Patch: shown as an empty slot (NOT SENT if just deleted here).
            if (lib.isUnsent ({ row.peakBank, row.slot }))
            {
                g.setColour (accent);
                g.fillRect (r.getX(), r.getY() + 2, 2, r.getHeight() - 4);
                g.setFont (font (7.5f));
                g.drawText ("NOT SENT", text.removeFromRight (56 + categoryWidth).removeFromLeft (56), juce::Justification::centredRight, false);
            }
            g.setFont (font (9.0f));
            g.setColour (silkscreen.withAlpha (0.3f));
            g.drawText (label, text.removeFromLeft (36), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.0f).withStyle ("Italic"));
            g.drawText ("Empty", text, juce::Justification::centredLeft, true);
            return;
        }

        const bool hovered = hoverRow == i;
        const bool favourite = entry != nullptr ? entry->favourite : lib.isPeakFavourite (*patch);
        g.setColour (favourite ? star : silkscreen.withAlpha (0.22f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::String::fromUTF8 (favourite ? "\xe2\x98\x85" : "\xe2\x98\x86"), r.withWidth (starWidth), juce::Justification::centred);
        for (int icon = exportIcon; icon <= deleteIcon; ++icon)
        {

            const auto box = juce::Rectangle<float> ((float) (starWidth + iconWidth * (icon - exportIcon)), (float) r.getY(), (float) iconWidth, (float) rowHeight);
            const bool hot = hovered && iconAt (hoverX) == icon;
            drawIcon (g, icon, box, hot ? (icon == deleteIcon ? accent : silkscreen) : silkscreen.withAlpha (hovered ? 0.55f : 0.28f));
        }

        if (owner.loadedKey == key) // the patch last auditioned
        {
            const auto c = juce::Point<float> ((float) iconsRight + 7.0f, (float) r.getCentreY());
            g.setColour (accent.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c));
            g.setColour (accent);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
        }

        g.setFont (font (9.0f));
        g.setColour (silkscreen.withAlpha (0.5f));
        g.drawText (label, text.removeFromLeft (36), juce::Justification::centredLeft);

        // Category (stored in the patch, byte 0x20) at the far right.
        {
            const auto& categoryParam = *peak::findParam ("patch_category");
            const auto category = patch->get (categoryParam);
            const bool hotCategory = hovered && inCategoryZone (hoverX);
            g.setFont (juce::FontOptions (12.0f));
            auto categoryArea = text.removeFromRight (categoryWidth);
            if (row.peakBank >= 0 && lib.isUnsent ({ row.peakBank, row.slot }))
            {
                // Changed here, not yet on the synth: amber edge + tag until Save all changes.
                g.setColour (accent);
                g.fillRect (r.getX(), r.getY() + 2, 2, r.getHeight() - 4);
                g.setFont (font (7.5f));
                g.drawText ("NOT SENT", text.removeFromRight (56), juce::Justification::centredRight, false);
                g.setFont (juce::FontOptions (12.0f));
            }
            if (category > 0 || hotCategory)
            {
                g.setColour (hotCategory ? accent : silkscreen.withAlpha (0.45f));
                g.drawText (category > 0 ? juce::String (peak::formatValue (categoryParam, category)) : juce::String ("category"), categoryArea,
                            juce::Justification::centredRight, false);
            }

        }

        if (key == owner.homeKey && owner.homeEdited)
        {
            // The editor's sound started here and has been changed since.
            g.setColour (accent);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold)); // as large as the category
            g.drawText ("EDITED", text.removeFromRight (64), juce::Justification::centredRight, false);
        }

        g.setFont (juce::FontOptions (14.0f));
        g.setColour (silkscreen);
        g.drawText (nameOf (*patch), text, juce::Justification::centredLeft, true);
    }

    LibraryDrawer& owner;
    const int pane;
    static constexpr int categoryWidth = 64, rightMargin = 14;
    int hoverRow = -1, hoverX = 0, dropRow = -1, pendingSelect = -1, dragCount = 1;
    bool dragStarted = false, dropBelow = false;
};

namespace
{
constexpr int savePanelHeight = 190;
}

//==============================================================================
// Save card: what's being saved, where to, what can be heard now, and Cancel / Confirm
// (as in the TEO-5 and OB-6 editors).
class LibraryDrawer::SavePanel final : public juce::Component
{
public:
    explicit SavePanel (LibraryDrawer& o) : owner (o)
    {
        for (auto* b : { &cancel, &confirm })
        {
            DrawerStyle::style (*b);
            addAndMakeVisible (b);
        }
        confirm.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3020));
        cancel.onClick = [this] { owner.cancelSave(); };
        confirm.onClick = [this] { owner.confirmSave(); };
    }

    void update()
    {
        confirm.setEnabled (owner.saveTarget.isNotEmpty());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff26282d));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

        auto r = getLocalBounds().reduced (18, 12);
        r.removeFromBottom (36); // buttons
        g.setColour (silkscreen);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("Save patch", r.removeFromTop (22), juce::Justification::centredLeft);
        r.removeFromTop (4);

        const auto line = [&] (const juce::String& label, const juce::String& text, juce::Colour c) {
            auto row = r.removeFromTop (20);
            g.setFont (juce::FontOptions (12.5f));
            g.setColour (silkscreen.withAlpha (0.6f));
            g.drawText (label, row.removeFromLeft (70), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.5f));
            g.setColour (c);
            g.drawFittedText (text, row, juce::Justification::centredLeft, 1, 0.85f);
        };
        const auto& patch = owner.model.getPatch();
        const auto home = loadedPatchKey (owner.model);
        auto from = home.startsWith ("p:") ? juce::String (owner.model.getOriginalLocation()->name())
                    : home.startsWith ("l:") ? juce::String ("My Banks") : juce::String();
        if (owner.model.isModified())
            from += from.isEmpty() ? "your edit" : " (edited)";
        line ("Saving", "\"" + nameOf (patch) + "\"" + (from.isNotEmpty() ? ", from " + from : juce::String()), silkscreen);

        bool replacing = false;
        const auto where = owner.describeTarget (replacing);
        const bool chosen = owner.saveTarget.isNotEmpty(), toPeak = owner.saveTarget.startsWith ("p:");
        line ("Save to", chosen ? where : juce::String ("Click a slot in the library"), chosen ? accent : silkscreen.withAlpha (0.5f));
        r.removeFromTop (6);

        g.setColour (silkscreen.withAlpha (0.85f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (owner.auditioned ? juce::String ("Now in the Peak's edit buffer: the patch you'd replace. Play the Peak "
                                                           "or your controller to hear it.")
                          : chosen && replacing ? juce::String ("Your edit is in the edit buffer. Click the slot to hear the patch it holds now.")
                          : chosen ? juce::String ("This slot is empty, so there's nothing to audition: the editor's keyboard is silent until you pick a slot with a patch.")
                                   : juce::String ("Select a slot and its patch goes into the Peak's edit buffer, so you can play it "
                                                   "before replacing it."),
                          r.removeFromTop (34), juce::Justification::topLeft, 2, 1.0f);
        if (chosen)
        {
            g.setFont (juce::FontOptions (12.5f));
            g.setColour (toPeak ? juce::Colour (0xfff0c27a) : silkscreen.withAlpha (0.6f));
            const auto slot = where.upToFirstOccurrenceOf (",", false, false);
            g.drawFittedText (toPeak ? juce::String::fromUTF8 ("\xe2\x9a\xa0 Confirm writes \"") + nameOf (patch) + "\" into the Peak's memory at " + slot
                                           + " (the bank is backed up first)."
                                     : juce::String ("Saved in My Banks only. Nothing is written to the Peak."),
                              r.removeFromTop (20), juce::Justification::topLeft, 1, 0.9f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 12).removeFromBottom (28);
        confirm.setBounds (r.removeFromRight (110));
        r.removeFromRight (10);
        cancel.setBounds (r.removeFromRight (110));
    }

private:
    LibraryDrawer& owner;
    juce::TextButton cancel { "Cancel" }, confirm { "Confirm" };
};

void LibraryDrawer::beginSave()
{
    if (! model.hasPatch())
        return setStatus ("Nothing to save: no patch loaded");
    saving = true;
    auditioned = false;
    selection.clear();

    // The patch's own slot is chosen to begin with; otherwise pick one.
    saveTarget = loadedPatchKey (model);
    if (saveTarget.startsWith ("p:"))
        expanded.insert ("pb:" + saveTarget.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false));
    else if (saveTarget.startsWith ("l:"))
        if (const auto* e = library.find (saveTarget.substring (2)))
            expanded.insert ("lb:" + e->bank);
    rebuildRows();
    for (int pane = 0; pane < 2; ++pane)
        for (int i = 0; i < (int) rows (pane).size(); ++i)
            if (rows (pane)[(size_t) i].key() == saveTarget)
                viewports[(size_t) pane].setViewPosition (0, juce::jmax (0, i * rowHeight - viewports[(size_t) pane].getHeight() / 3));

    savePanel->setVisible (true);
    savePanel->update();
    resized();
    setStatus ("Choose where to save: click a slot in a Peak bank or in My Banks (Return confirms, Esc cancels)");
    grabKeyboardFocus();
    for (auto& l : lists)
        l->repaint();
}

juce::String LibraryDrawer::describeTarget (bool& replacing) const
{
    replacing = false;
    if (saveTarget.startsWith ("new:"))
        return saveTarget.substring (4) + ", new (at the end)";
    const auto p = patchFor (saveTarget);
    replacing = p && ! isEmpty (*p);
    juce::String where;
    if (saveTarget.startsWith ("p:"))
        where = p && p->location() ? juce::String (p->location()->name()) : saveTarget.substring (2);
    else if (const auto* e = library.find (saveTarget.substring (2)))
        where = e->bank;
    return where + (replacing ? ", replacing \"" + nameOf (*p) + "\"" : juce::String (" (empty)"));
}

void LibraryDrawer::setSaveTarget (const juce::String& key)
{
    if (! saving || key.isEmpty() || key.startsWith ("pb:") || key.startsWith ("lb:"))
        return;
    saveTarget = key;
    // Audition: the patch stored there goes into the edit buffer (the editor's patch doesn't change).
    // An empty slot has nothing to hear, so the keyboard is silent until a slot with a patch is chosen.
    const auto p = patchFor (key);
    const bool empty = ! p || isEmpty (*p);
    model.getConnection().setNotesMuted (empty);
    if (! empty)
    {
        auto sound = *p;
        sound.setLocation (std::nullopt);
        model.getConnection().sendSysex (sound.toSysex());
        auditioned = true;
    }
    else if (std::exchange (auditioned, false))
        model.sendPatchToPeak(); // your edit back (silent until another slot is picked)
    setStatus ({});
    savePanel->update();
    for (auto& l : lists)
        l->repaint();
}

void LibraryDrawer::confirmSave()
{
    if (! saving || saveTarget.isEmpty())
        return;
    const auto name = nameOf (model.getPatch());
    juce::String result;
    if (saveTarget.startsWith ("p:"))
    {
        const peak::Patch::Location where { saveTarget.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                            saveTarget.fromLastOccurrenceOf (":", false, false).getIntValue() };
        bool ok = false;
        result = SlotWriter::write (model, where, ok);
        if (! ok)
            return setStatus (result); // stay in the picker
    }
    else
    {
        library.checkpoint();
        juce::String id;
        if (saveTarget.startsWith ("new:"))
            id = library.add (model.getPatch(), "Editor, " + today(), saveTarget.substring (4));
        else
        {
            id = saveTarget.substring (2);
            library.replacePatch (id, model.getPatch());
        }
        model.markSaved (std::nullopt, "My Banks: " + name);
        model.setLoadedFrom ("l:" + id);
        loadedKey = "l:" + id;
        result = "Saved \"" + name + "\" in My Banks";
    }
    if (auditioned)
        model.sendPatchToPeak(); // the saved patch is in the edit buffer, as stored from now on
    model.getConnection().setNotesMuted (false);
    saving = auditioned = false;
    saveTarget.clear();
    savePanel->setVisible (false);
    resized();
    setStatus (result);
    revealLoadedPatch();
    if (onSaveFinished)
        onSaveFinished (true, result);
}

void LibraryDrawer::cancelSave (bool notify)
{
    if (! saving)
        return;
    if (std::exchange (auditioned, false))
        model.sendPatchToPeak(); // your edit goes back
    model.getConnection().setNotesMuted (false);
    saving = false;
    saveTarget.clear();
    savePanel->setVisible (false);
    resized();
    setStatus ("Not saved");
    for (auto& l : lists)
        l->repaint();
    if (notify && onSaveFinished)
        onSaveFinished (false, "Not saved");
}

//==============================================================================
LibraryDrawer::LibraryDrawer (PeakModel& m) : model (m), library (m.getLibrary())
{
    savePanel = std::make_unique<SavePanel> (*this);
    addChildComponent (*savePanel);
    title.setFont (font (14.0f));
    title.setColour (juce::Label::textColourId, silkscreen);
    addAndMakeVisible (title);

    setWantsKeyboardFocus (true);
    for (auto* b : { &helpButton, &fetchButton, &importButton, &saveAllButton, &newBankButton, &undoButton, &redoButton, &pullAllButton, &pushButton })
    {
        DrawerStyle::style (*b);
        addAndMakeVisible (b);
    }
    helpButton.setTooltip ("How the library works");
    fetchButton.setTooltip ("Read banks from the Peak: one bank or all four (click again to stop)");
    importButton.setTooltip ("Import .syx files (single patches or banks) into My Banks");
    newBankButton.setTooltip ("Make a new bank in My Banks");
    saveAllButton.setTooltip ("Write every NOT SENT change to the Peak (lists what it overwrites and backs up each bank first)");
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Shift+Cmd+Z)");
    pullAllButton.setTooltip ("Copy every bank read from the Peak into My Banks");
    pushButton.setTooltip ("Put one of your banks into a Peak bank, as NOT SENT changes");

    helpButton.onClick = [this] {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Library").withMessage (helpText).withButton ("OK", 0).withAssociatedComponent (this), [] (int) {});
    };
    fetchButton.onClick = [this] {
        if (fetchQueue.empty())
            fetchMenu();
        else
            stopFetch ("Stopped");
    };
    model.onSlotChangedOutside = [this] (peak::Patch::Location where, const juce::File& backup) {
        warnOutOfSync (juce::String (where.name()), backup.exists() ? juce::StringArray { backup.getFileName() } : juce::StringArray {});
    };
    // Once the editor has its current sound, check all four banks against its copy.
    juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<LibraryDrawer> (this)] {
        if (safe != nullptr)
            safe->startSyncCheck();
    });
    model.onSlotStored = [this] (peak::Patch::Location where) {
        // The slot asked for has arrived: on to the next.
        if (fetchIndex < fetchQueue.size() && where == fetchQueue[fetchIndex])
        {
            // Startup check: has this slot changed since the editor last read it?
            if (syncCheck)
                if (const auto& before = syncBefore[(size_t) where.bank][(size_t) where.slot])
                    if (const auto& now = library.syncedSlot (where); now && ! now->sameSound (*before))
                        syncChanged[(size_t) where.bank].add (juce::String (where.name()));
            // Checking a save: does the slot now hold what was written?
            if (const auto it = fetchExpected.find (where.bank * 128 + where.slot); it != fetchExpected.end())
                if (const auto& got = library.syncedSlot (where); ! got || ! got->sameSound (it->second))
                    fetchMismatches.add (juce::String (where.name()));
            ++fetchIndex;
            fetchRetries = 0;
            fetchNext();
        }
    };
    importButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Import patches (.syx, single or bank)", juce::File(), "*.syx");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) {
                                  juce::StringArray paths;
                                  for (const auto& f : fc.getResults())
                                      paths.add (f.getFullPathName());
                                  if (! paths.isEmpty())
                                      filesDropped (paths, 0, 0);
                              });
    };
    newBankButton.onClick = [this] {
        askText ("New bank", "Bank name:", "New Bank", [this] (const juce::String& n) {
            expanded.insert ("lb:" + library.addBank (n));
            rebuildRows();
        });
    };
    saveAllButton.onClick = [this] { saveAllChanges(); };
    undoButton.onClick = [this] {
        library.undo();
        setStatus ("Undone");
    };
    redoButton.onClick = [this] {
        library.redo();
        setStatus ("Redone");
    };
    pullAllButton.onClick = [this] { copyPeakToLibrary(); };
    pushButton.onClick = [this] {
        juce::PopupMenu menu;
        for (const auto& bank : library.getBanks())
        {
            juce::PopupMenu into;
            for (int b = 0; b < 4; ++b)
                into.addItem ("Into Peak bank " + bankLetter (b), [this, bank, b] { sendBankToPeak (bank, b); });
            menu.addSubMenu (bank + "  (" + juce::String ((int) library.entriesIn (bank).size()) + ")", into, ! library.entriesIn (bank).empty());
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pushButton));
    };

    search.setTextToShowWhenEmpty ("Search names (or cat:pad)", silkscreen.withAlpha (0.4f));
    DrawerStyle::style (search);
    search.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible (search);
    DrawerStyle::style (favouritesOnly);
    favouritesOnly.onClick = [this] { rebuildRows(); };
    addAndMakeVisible (favouritesOnly);

    categoryFilter.addItem ("All categories", 1);
    for (size_t i = 1; i < peak::categoryNames().size(); ++i)
        categoryFilter.addItem (peak::categoryNames()[i], (int) i + 2);
    categoryFilter.setSelectedId (1, juce::dontSendNotification);
    categoryFilter.setColour (juce::ComboBox::backgroundColourId, field);
    categoryFilter.setColour (juce::ComboBox::textColourId, silkscreen);
    categoryFilter.setColour (juce::ComboBox::outlineColourId, pinstripe.withAlpha (0.6f));
    categoryFilter.setTooltip ("Show one category (the Peak's patch category)");
    categoryFilter.onChange = [this] { rebuildRows(); };
    addAndMakeVisible (categoryFilter);

    const char* names[] = { "PEAK", "MY BANKS" };
    const char* hints[] = { "Banks A\xe2\x80\x93" "D, as read from the Peak", "Imported .syx files and your own banks" };
    for (int pane = 0; pane < 2; ++pane)
    {
        lists[(size_t) pane] = std::make_unique<ListView> (*this, pane);
        auto& vp = viewports[(size_t) pane];
        vp.setViewedComponent (lists[(size_t) pane].get(), false);
        vp.setScrollBarsShown (true, false);
        vp.setScrollBarThickness (8);
        addAndMakeVisible (vp);

        paneTitles[(size_t) pane].setText (names[pane], juce::dontSendNotification);
        paneTitles[(size_t) pane].setFont (font (11.0f));
        paneTitles[(size_t) pane].setColour (juce::Label::textColourId, silkscreen);
        paneHints[(size_t) pane].setText (juce::String::fromUTF8 (hints[pane]), juce::dontSendNotification);
        paneHints[(size_t) pane].setFont (juce::FontOptions (12.0f));
        paneHints[(size_t) pane].setJustificationType (juce::Justification::centredRight);
        paneHints[(size_t) pane].setColour (juce::Label::textColourId, silkscreen.withAlpha (0.6f));
        addAndMakeVisible (paneTitles[(size_t) pane]);
        addAndMakeVisible (paneHints[(size_t) pane]);
    }

    status.setFont (juce::FontOptions (12.5f));
    status.setColour (juce::Label::textColourId, silkscreen.withAlpha (0.65f));
    addAndMakeVisible (status);

    library.changed.addChangeListener (this);
    model.changed.addChangeListener (this);
    rebuildRows();
    updateToolbar();
}

LibraryDrawer::~LibraryDrawer()
{
    model.onSlotStored = nullptr;
    model.onSlotChangedOutside = nullptr;
    library.changed.removeChangeListener (this);
    model.changed.removeChangeListener (this);
}

void LibraryDrawer::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (pinstripe);
    g.fillRect (getWidth() - 2, 0, 2, getHeight());
    g.fillRect (12, 34, getWidth() - 24, 1);

    // Divider between the panes, with a hint that patches travel between them.
    const int x = (viewports[0].getRight() + viewports[1].getX()) / 2;
    g.setColour (pinstripe.withAlpha (0.5f));
    g.fillRect (x, paneTitles[0].getY(), 1, viewports[0].getBottom() - paneTitles[0].getY());
    g.setColour (silkscreen.withAlpha (0.55f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x87\x84"), juce::Rectangle<int> (x - 9, viewports[0].getY() - 22, 18, 18), juce::Justification::centred, false);
}

void LibraryDrawer::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromRight (2);

    auto top = r.removeFromTop (24);
    helpButton.setBounds (top.removeFromLeft (22).withSizeKeepingCentre (20, 20));
    top.removeFromLeft (6);
    title.setBounds (top);
    r.removeFromTop (8);

    auto buttons = r.removeFromTop (24);
    const int bw = (buttons.getWidth() - 5 * 4) / 6;
    for (auto* b : { &fetchButton, &importButton, &saveAllButton, &newBankButton, &undoButton, &redoButton })
    {
        b->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (4);
    }
    r.removeFromTop (4);
    auto row = r.removeFromTop (24);
    pullAllButton.setBounds (row.removeFromLeft ((row.getWidth() - 4) / 2));
    row.removeFromLeft (4);
    pushButton.setBounds (row);
    r.removeFromTop (6);

    auto searchRow = r.removeFromTop (24);
    favouritesOnly.setBounds (searchRow.removeFromRight (100));
    searchRow.removeFromRight (6);
    categoryFilter.setBounds (searchRow.removeFromRight (140));
    searchRow.removeFromRight (6);
    search.setBounds (searchRow);
    r.removeFromTop (6);

    status.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (4);
    if (saving)
    {
        // The save card sits under the two panes, centred.
        auto card = r.removeFromBottom (savePanelHeight);
        savePanel->setBounds (card.withSizeKeepingCentre (juce::jmin (620, card.getWidth()), savePanelHeight));
        r.removeFromBottom (8);
    }

    const int gap = 16, paneW = (r.getWidth() - gap) / 2;
    for (int pane = 0; pane < 2; ++pane)
    {
        auto col = pane == 0 ? r.removeFromLeft (paneW) : r.removeFromRight (paneW);
        auto head = col.removeFromTop (22);
        paneTitles[(size_t) pane].setBounds (head.removeFromLeft (90));
        paneHints[(size_t) pane].setBounds (head);
        col.removeFromTop (4);
        viewports[(size_t) pane].setBounds (col);
        lists[(size_t) pane]->updateSize (col.getWidth() - viewports[(size_t) pane].getScrollBarThickness());
    }
}

void LibraryDrawer::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &model.changed)
    {
        // Only the EDITED mark can change; rows are rebuilt for library changes.
        if (const auto key = findLoadedPatch(); key != homeKey || model.isModified() != homeEdited)
        {
            homeKey = key;
            homeEdited = model.isModified();
            for (auto& l : lists)
                l->repaint();
            updateToolbar();
        }
        return;
    }
    rebuildRows();
    updateToolbar();
}

juce::String LibraryDrawer::findLoadedPatch() const
{
    return loadedPatchKey (model);
}


void LibraryDrawer::visibilityChanged()
{
    if (isShowing())
    {
        if (! saving)
            revealLoadedPatch();
    }
    else if (saving)
        cancelSave (false); // left the Library mid-save
}

void LibraryDrawer::revealLoadedPatch()
{
    homeKey = findLoadedPatch();
    homeEdited = model.isModified();
    if (homeKey.isEmpty())
        return;

    // Open its bank, select it and scroll it into the middle of its pane.
    const auto pane = homeKey.startsWith ("p:") ? peakPane : libraryPane;
    if (pane == peakPane)
        expanded.insert ("pb:" + homeKey.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false));
    else if (const auto* e = library.find (homeKey.substring (2)))
        expanded.insert ("lb:" + e->bank);
    rebuildRows();
    selection.clear();
    selection.insert (homeKey);
    for (int i = 0; i < (int) rows (pane).size(); ++i)
        if (rows (pane)[(size_t) i].key() == homeKey)
        {
            auto& view = viewports[(size_t) pane];
            view.setViewPosition (view.getViewPositionX(), juce::jmax (0, i * rowHeight - (view.getViewHeight() - rowHeight) / 2));
            break;
        }
    for (auto& l : lists)
        l->repaint();
}

void LibraryDrawer::updateToolbar()
{
    const auto unsent = library.unsentCount() + (homeEdited && homeKey.isNotEmpty() ? 1 : 0); // + the edited patch
    saveAllButton.setEnabled (unsent > 0);
    saveAllButton.setButtonText (unsent > 0 ? "Save all changes (" + juce::String (unsent) + ")" : juce::String ("Save all changes"));
    saveAllButton.setColour (juce::TextButton::textColourOffId, unsent > 0 ? accent : silkscreen);
    undoButton.setEnabled (library.canUndo());
    redoButton.setEnabled (library.canRedo());
}

bool LibraryDrawer::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        undoButton.triggerClick();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        redoButton.triggerClick();
        return true;
    }
    if (saving)
    {
        if (key.isKeyCode (juce::KeyPress::returnKey))
            confirmSave();
        else if (key.isKeyCode (juce::KeyPress::escapeKey))
            cancelSave();
        return true;
    }
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (key.isKeyCode (juce::KeyPress::upKey))
        return navigate (-1, 0);
    if (key.isKeyCode (juce::KeyPress::downKey))
        return navigate (1, 0);
    if (key.isKeyCode (juce::KeyPress::leftKey))
        return navigate (0, -1);
    if (key.isKeyCode (juce::KeyPress::rightKey))
        return navigate (0, 1);
    return false;
}

bool LibraryDrawer::navigate (int rowDelta, int bankDelta)
{
    // Start from the selected patch (or the one last loaded) in whichever pane holds it.
    int pane = -1, row = -1;
    for (int p = 0; p < 2 && row < 0; ++p)
        for (int i = 0; i < (int) rows (p).size(); ++i)
            if (const auto k = rows (p)[(size_t) i].key(); ! rows (p)[(size_t) i].isBank && (selection.count (k) || k == loadedKey))
            {
                pane = p;
                row = i;
                if (selection.count (k))
                    break;
            }
    if (row < 0)
        return false;

    const auto& list = rows (pane);
    const auto isPatch = [&] (int i) { return juce::isPositiveAndBelow (i, (int) list.size()) && ! list[(size_t) i].isBank && ! list[(size_t) i].isNew; };
    int target = -1;
    if (rowDelta != 0)
    {
        for (int i = row + rowDelta; juce::isPositiveAndBelow (i, (int) list.size()); i += rowDelta)
            if (isPatch (i))
            {
                target = i;
                break;
            }
    }
    else
    {
        // The bank header above this row, and the position within the bank.
        int header = row;
        while (header >= 0 && ! list[(size_t) header].isBank)
            --header;
        const int position = row - header;
        // The next bank in that direction that is open (has patch rows).
        for (int h = header + bankDelta; juce::isPositiveAndBelow (h, (int) list.size()); h += bankDelta)
            if (list[(size_t) h].isBank && isPatch (h + 1))
            {
                int last = h + 1;
                while (isPatch (last + 1))
                    ++last;
                target = juce::jmin (h + position, last);
                break;
            }
    }
    if (target < 0)
        return true; // at the end: nothing to move to

    const auto key = list[(size_t) target].key();
    selection.clear();
    selection.insert (key);
    auto& view = viewports[(size_t) pane];
    const auto top = target * rowHeight;
    if (top < view.getViewPositionY())
        view.setViewPosition (view.getViewPositionX(), top);
    else if (top + rowHeight > view.getViewPositionY() + view.getViewHeight())
        view.setViewPosition (view.getViewPositionX(), top + rowHeight - view.getViewHeight());
    audition (key);
    return true;
}

//==============================================================================
bool LibraryDrawer::matches (const peak::Patch& p, bool favourite) const
{
    if (favouritesOnly.getToggleState() && ! favourite)
        return false;
    if (categoryFilter.getSelectedId() > 1 && p.get (*peak::findParam ("patch_category")) != categoryFilter.getSelectedId() - 2)
        return false;
    const auto q = search.getText().trim().toLowerCase();
    if (q.isEmpty())
        return true;
    if (q.startsWith ("cat:"))
    {
        const auto want = q.substring (4).trim();
        const auto& param = *peak::findParam ("patch_category");
        const auto category = p.get (param);
        return want.containsOnly ("0123456789") ? category == want.getIntValue()
                                                : juce::String (peak::formatValue (param, category)).toLowerCase().startsWith (want);
    }
    return juce::String (p.name()).toLowerCase().contains (q);
}

void LibraryDrawer::rebuildRows()
{
    const bool filtering = search.getText().trim().isNotEmpty() || favouritesOnly.getToggleState() || categoryFilter.getSelectedId() > 1;

    auto& peakRows = rowLists[(size_t) peakPane];
    peakRows.clear();
    for (int b = 0; b < 4; ++b)
    {
        Row header;
        header.isBank = true;
        header.peakBank = b;
        peakRows.push_back (header);
        if (! filtering && isCollapsed (header))
            continue;
        for (int s = 0; s < 128; ++s)
        {
            const auto& p = library.peakSlot ({ b, s });
            if (filtering && (! p || ! matches (*p, library.isPeakFavourite (*p))))
                continue;
            Row r;
            r.peakBank = b;
            r.slot = s;
            peakRows.push_back (r);
        }
    }

    auto& libRows = rowLists[(size_t) libraryPane];
    libRows.clear();
    for (const auto& bank : library.getBanks())
    {
        Row header;
        header.isBank = true;
        header.bank = bank;
        libRows.push_back (header);
        if (! filtering && isCollapsed (header))
            continue;
        int index = 0;
        const auto entriesHere = library.entriesIn (bank);
        for (const auto* e : entriesHere)
        {
            const auto i = index++;
            if (! matches (e->patch, e->favourite))
                continue;
            Row r;
            r.bank = bank;
            r.id = e->id;
            r.index = i;
            libRows.push_back (r);
        }
        if (! filtering)
        {
            Row empty; // always an empty slot to drop onto at the end of the bank
            empty.isNew = true;
            empty.bank = bank;
            empty.index = (int) entriesHere.size();
            libRows.push_back (empty);
        }
    }

    for (int pane = 0; pane < 2; ++pane)
    {
        lists[(size_t) pane]->updateSize (viewports[(size_t) pane].getMaximumVisibleWidth());
        lists[(size_t) pane]->repaint();
    }
}

std::optional<peak::Patch> LibraryDrawer::patchFor (const juce::String& key) const
{
    if (key.startsWith ("p:"))
    {
        const auto parts = juce::StringArray::fromTokens (key.substring (2), ":", "");
        return library.peakSlot ({ parts[0].getIntValue(), parts[1].getIntValue() });
    }
    if (key.startsWith ("l:"))
        if (const auto* e = library.find (key.substring (2)))
            return e->patch;
    return std::nullopt;
}

bool LibraryDrawer::isCollapsed (const Row& r) const
{
    return expanded.count (r.key()) == 0; // banks start collapsed
}

void LibraryDrawer::toggleCollapsed (const Row& r)
{
    const auto key = r.key();
    if (! expanded.erase (key))
        expanded.insert (key);
    rebuildRows();
}

juce::StringArray LibraryDrawer::selectedKeys (int pane) const
{
    juce::StringArray keys;
    for (const auto& r : rows (pane))
        if (! r.isBank && selection.count (r.key()) && patchFor (r.key()))
            keys.add (r.key());
    return keys;
}

void LibraryDrawer::rowClicked (int pane, int row, const juce::MouseEvent& e)
{
    const auto key = rows (pane)[(size_t) row].key();
    if (e.mods.isCommandDown())
    {
        if (! selection.erase (key))
            selection.insert (key);
    }
    else if (e.mods.isShiftDown() && ! selection.empty())
    {
        // Extend from the nearest selected row in this pane.
        int anchor = -1;
        for (int i = 0; i < (int) rows (pane).size(); ++i)
            if (selection.count (rows (pane)[(size_t) i].key()) && (anchor < 0 || std::abs (i - row) < std::abs (anchor - row)))
                anchor = i;
        if (anchor < 0)
            anchor = row;
        for (int i = juce::jmin (anchor, row); i <= juce::jmax (anchor, row); ++i)
            if (! rows (pane)[(size_t) i].isBank)
                selection.insert (rows (pane)[(size_t) i].key());
    }
    else
    {
        selection.clear();
        selection.insert (key);
    }
    for (auto& l : lists)
        l->repaint();
}

void LibraryDrawer::rowDoubleClicked (int pane, int row)
{
    const auto& r = rows (pane)[(size_t) row];
    if (! r.isBank && ! r.isNew)
        audition (r.key());
}

void LibraryDrawer::audition (const juce::String& key)
{
    const auto p = patchFor (key);
    if (! p)
        return;
    const auto where = key.startsWith ("p:") ? juce::String (p->location()->name()) : "My Banks";
    if (model.audition (*p, where + ": " + nameOf (*p)))
    {
        loadedKey = key;
        model.setLoadedFrom (key);
        setStatus ("Auditioning \"" + nameOf (*p) + "\" (not saved)");
    }
    else
        setStatus ("No MIDI output to the Peak");
    for (auto& l : lists)
        l->repaint();
}

void LibraryDrawer::rowIconClicked (int pane, int row, int icon)
{
    const auto& r = rows (pane)[(size_t) row];
    const auto key = r.key();
    const auto patch = patchFor (key);
    if (! patch)
        return;
    const auto* entry = pane == libraryPane ? library.find (r.id) : nullptr;

    if (icon == starIcon)
    {
        library.checkpoint();
        if (entry != nullptr)
            library.setFavourite (r.id, ! entry->favourite);
        else
            library.setPeakFavourite (*patch, ! library.isPeakFavourite (*patch));
    }
    else if (icon == exportIcon)
        exportKeys ({ key }, nameOf (*patch));
    else if (icon == renameIcon && entry == nullptr)
        renameOnPeak (*patch);
    else if (icon == deleteIcon && entry == nullptr)
    {
        auto keys = selectedKeys (peakPane);
        if (! keys.contains (key))
            keys = { key };
        emptyPeakSlots (keys);
    }
    else if (icon == renameIcon && entry != nullptr)
        askText ("Rename", "Name (16 characters):", nameOf (*patch), [this, id = r.id] (const juce::String& s) { library.rename (id, s.substring (0, 16)); });
    else if (icon == deleteIcon && entry != nullptr)
        juce::AlertWindow::showAsync (confirmOptions ("Delete", "Move \"" + nameOf (*patch) + "\" to the Trash?", "Delete", this), [this, id = r.id] (int res) {
            if (res == 1)
            {
                library.checkpoint();
                library.remove (id);
            }
        });
}

void LibraryDrawer::rowMenu (int pane, int row)
{
    const auto r = rows (pane)[(size_t) row];
    if (r.isNew)
        return;
    juce::PopupMenu menu;

    if (r.isBank && r.peakBank >= 0)
    {
        const auto b = r.peakBank;
        const auto known = library.knownSlots (b);
        menu.addItem ("Copy to My Banks", known > 0, false, [this, b] {
            juce::StringArray keys;
            for (int s = 0; s < 128; ++s)
                if (library.peakSlot ({ b, s }))
                    keys.add ("p:" + juce::String (b) + ":" + juce::String (s));
            addToBank (keys, "Peak Bank " + bankLetter (b) + " " + today());
        });
        menu.addItem (juce::String::fromUTF8 ("Export bank .syx…"), known > 0, false, [this, b] {
            juce::StringArray keys;
            for (int s = 0; s < 128; ++s)
                if (library.peakSlot ({ b, s }))
                    keys.add ("p:" + juce::String (b) + ":" + juce::String (s));
            exportKeys (keys, "Peak Bank " + bankLetter (b));
        });
        menu.addItem ("Back up now", known == 128, false, [this, b] {
            juce::File f;
            const auto res = library.backupBank (b, &f);
            setStatus (res ? "Backed up to " + f.getFileName() : res.getErrorMessage());
        });
        menu.addItem ("Fetch this bank from the Peak", fetchQueue.empty(), false, [this, b] { startFetch ({ b }); });
        if (const auto n = library.unsentCount (b); n > 0)
        {
            menu.addSeparator();
            menu.addItem ("Discard " + juce::String (n) + " NOT SENT change(s)", [this, b] {
                library.checkpoint();
                library.discardAllUnsent (b);
            });
        }
    }
    else if (r.isBank)
    {
        const auto bank = r.bank;
        menu.addItem ("Store editor patch here", [this, bank] { storeEditorPatch (bank); });
        menu.addItem (juce::String::fromUTF8 ("Rename bank…"), [this, bank] {
            askText ("Rename bank", "Bank name:", bank, [this, bank] (const juce::String& n) { library.renameBank (bank, n); });
        });
        menu.addItem (juce::String::fromUTF8 ("Export bank .syx…"), ! library.entriesIn (bank).empty(), false, [this, bank] {
            juce::StringArray keys;
            for (const auto* e : library.entriesIn (bank))
                keys.add ("l:" + e->id);
            exportKeys (keys, bank);
        });
        juce::PopupMenu into;
        for (int b = 0; b < 4; ++b)
            into.addItem ("Into Peak bank " + bankLetter (b), [this, bank, b] { sendBankToPeak (bank, b); });
        menu.addSubMenu ("Send to the Peak", into, ! library.entriesIn (bank).empty());
        menu.addSeparator();
        menu.addItem (juce::String::fromUTF8 ("Delete bank…"), [this, bank] {
            const auto n = (int) library.entriesIn (bank).size();
            juce::AlertWindow::showAsync (confirmOptions ("Delete bank", "Delete \"" + bank + "\"" + (n > 0 ? " and move its " + juce::String (n) + " patch(es) to the Trash?" : "?"), "Delete", this),
                                          [this, bank] (int res) {
                                              if (res == 1)
                                              {
                                                  library.checkpoint();
                                                  library.removeBank (bank);
                                              }
                                          });
        });
    }
    else
    {
        auto keys = selectedKeys (pane);
        if (keys.isEmpty())
            keys.add (r.key());
        const auto single = keys.size() == 1;

        menu.addItem ("Audition", single, false, [this, k = keys[0]] { audition (k); });
        menu.addSubMenu ("Category", categoryMenu (r.key()), pane == libraryPane || single);

        juce::PopupMenu toBank;
        for (const auto& bank : library.getBanks())
            toBank.addItem (bank, [this, keys, bank] { addToBank (keys, bank); });
        toBank.addSeparator();
        toBank.addItem (juce::String::fromUTF8 ("New bank…"), [this, keys] {
            askText ("New bank", "Bank name:", "New Bank", [this, keys] (const juce::String& n) { addToBank (keys, library.addBank (n), -1, false); });
        });
        menu.addSubMenu (pane == peakPane ? "Copy to My Banks" : "Move to bank", toBank);

        if (pane == peakPane)
        {
            if (const auto p = patchFor (keys[0]))
            {
                menu.addItem (juce::String::fromUTF8 ("Rename on the Peak…"), single, false, [this, patch = *p] { renameOnPeak (patch); });
                menu.addItem (library.isPeakFavourite (*p) ? "Remove from favourites" : "Add to favourites", [this, keys] {
                    library.checkpoint();
                    for (const auto& k : keys)
                        if (const auto x = patchFor (k))
                            library.setPeakFavourite (*x, ! library.isPeakFavourite (*x));
                });
            }
            menu.addItem (keys.size() == 1 ? juce::String ("Delete (leave the slot empty)") : "Delete " + juce::String (keys.size()) + " (leave the slots empty)",
                          [this, keys] { emptyPeakSlots (keys); });
            if (library.isUnsent ({ r.peakBank, r.slot }))
                menu.addItem ("Discard change (keep what's on the Peak)", [this, loc = peak::Patch::Location { r.peakBank, r.slot }] {
                    library.checkpoint();
                    library.discardUnsent (loc);
                });
            menu.addItem (juce::String::fromUTF8 ("Write editor patch here now…"), single, false, [this, b = r.peakBank, s = r.slot] {
                SlotWriter::confirmAndWrite (model, { b, s }, this, [this] (const juce::String& res) { setStatus (res); });
            });
        }
        else if (const auto* e = library.find (r.id))
        {
            menu.addItem (juce::String::fromUTF8 ("Rename…"), single, false, [this, id = r.id, n = nameOf (e->patch)] {
                askText ("Rename", "Name (16 characters):", n, [this, id] (const juce::String& s) { library.rename (id, s.substring (0, 16)); });
            });
            menu.addItem (e->favourite ? "Remove from favourites" : "Add to favourites", [this, keys] {
                library.checkpoint();
                for (const auto& k : keys)
                    if (const auto* x = library.find (k.substring (2)))
                        library.setFavourite (x->id, ! x->favourite);
            });
        }
        menu.addItem (juce::String::fromUTF8 ("Export .syx…"), [this, keys] { exportKeys (keys, keys.size() == 1 ? nameOf (*patchFor (keys[0])) : juce::String ("peak-patches")); });
        if (pane == libraryPane)
        {
            menu.addSeparator();
            menu.addItem (juce::String::fromUTF8 ("Delete…"), [this, keys] {
                juce::AlertWindow::showAsync (confirmOptions ("Delete", "Move " + juce::String (keys.size()) + " patch(es) to the Trash?", "Delete", this), [this, keys] (int res) {
                    if (res == 1)
                    {
                        library.checkpoint();
                        for (const auto& k : keys)
                            library.remove (k.substring (2));
                    }
                });
            });
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void LibraryDrawer::dropKeys (const juce::StringArray& keys, int pane, int row, bool below)
{
    const auto& list = rows (pane);
    if (pane == libraryPane)
    {
        // Insert before (or after) the row under the cursor; on a bank header, at its start.
        auto bank = PatchLibrary::defaultBank;
        int index = -1;
        if (juce::isPositiveAndBelow (row, (int) list.size()))
        {
            const auto& target = list[(size_t) row];
            bank = target.bank;
            index = target.isBank ? 0 : target.isNew ? target.index : target.index + (below ? 1 : 0);
            // Moving within a bank: rows above the drop point that move don't count.
            for (const auto& k : keys)
                if (const auto* e = k.startsWith ("l:") ? library.find (k.substring (2)) : nullptr; e != nullptr && e->bank == bank)
                {
                    const auto entriesHere = library.entriesIn (bank);
                    for (int i = 0; i < juce::jmin (index, (int) entriesHere.size()); ++i)
                        if (entriesHere[(size_t) i]->id == e->id)
                        {
                            --index;
                            break;
                        }
                }
        }
        else if (! library.getBanks().isEmpty())
            bank = library.getBanks()[library.getBanks().size() - 1];
        addToBank (keys, bank, index);
        return;
    }

    if (! juce::isPositiveAndBelow (row, (int) list.size()))
        return;
    const auto& target = list[(size_t) row];
    const peak::Patch::Location start { target.peakBank, target.isBank ? 0 : target.slot };

    bool allFromPeak = true;
    for (const auto& k : keys)
        allFromPeak = allFromPeak && k.startsWith ("p:");
    if (allFromPeak)
        return swapInPeak (keys, start);

    std::vector<peak::Patch> patches;
    for (const auto& k : keys)
        if (const auto p = patchFor (k))
            patches.push_back (*p);
    if (! patches.empty())
        stageForPeak (patches, start, patches.size() == 1 ? "\"" + nameOf (patches[0]) + "\"" : juce::String ((int) patches.size()) + " patches");
}

void LibraryDrawer::addToBank (const juce::StringArray& keys, const juce::String& bank, int index, bool checkpointFirst)
{
    if (checkpointFirst)
        library.checkpoint();
    int added = 0, moved = 0, dups = 0;
    juce::StringArray placed; // ids to put at `index`, in drag order
    for (const auto& k : keys)
    {
        if (k.startsWith ("l:"))
        {
            if (const auto* e = library.find (k.substring (2)))
            {
                placed.add (e->id);
                ++moved;
            }
        }
        else if (const auto p = patchFor (k))
        {
            bool dup = false;
            const auto id = library.add (*p, "Peak " + juce::String (p->location()->name()) + ", " + today(), bank, &dup);
            dup ? ++dups : ++added;
            if (! dup)
                placed.add (id);
        }
    }
    if (! placed.isEmpty())
        library.moveEntries (placed, bank, index < 0 ? std::numeric_limits<int>::max() : index);
    expanded.insert ("lb:" + bank);
    rebuildRows();
    setStatus (bank + ": " + (added > 0 ? juce::String (added) + " copied " : juce::String()) + (moved > 0 ? juce::String (moved) + " moved " : juce::String())
               + (dups > 0 ? juce::String (dups) + " already there" : juce::String()));
}

void LibraryDrawer::exportKeys (const juce::StringArray& keys, const juce::String& suggestedName)
{
    if (keys.isEmpty())
        return setStatus ("Select patches to export");

    std::vector<peak::Bytes> messages;
    for (const auto& k : keys)
        if (const auto p = patchFor (k))
            messages.push_back (p->toSysex()); // Peak memory keeps its slot, so a re-import restores it

    const auto file = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::File::createLegalFileName (suggestedName) + ".syx");
    chooser = std::make_unique<juce::FileChooser> ("Export patches", file, "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, messages] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              const auto bytes = peak::joinSysex (messages);
                              setStatus (f.replaceWithData (bytes.data(), bytes.size()) ? "Exported " + juce::String ((int) messages.size()) + " patch(es) to " + f.getFileName()
                                                                                        : "Could not write " + f.getFullPathName());
                          });
}

void LibraryDrawer::stageForPeak (std::vector<peak::Patch> patches, peak::Patch::Location start, const juce::String& what, bool checkpointFirst)
{
    patches.resize (juce::jmin (patches.size(), (size_t) (128 - start.slot)));
    if (patches.empty())
        return;

    if (checkpointFirst)
        library.checkpoint();
    for (size_t i = 0; i < patches.size(); ++i)
    {
        auto p = patches[i];
        p.setLocation (peak::Patch::Location { start.bank, start.slot + (int) i });
        library.stagePeakSlot (p);
    }
    expanded.insert ("pb:" + juce::String (start.bank));
    rebuildRows();

    const auto first = juce::String (start.name());
    const auto range = patches.size() == 1 ? first : first + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (peak::Patch::Location { start.bank, start.slot + (int) patches.size() - 1 }.name());
    setStatus (what + " at " + range + ": NOT SENT. Save all changes writes " + juce::String (library.unsentCount()) + " change(s) to the Peak.");
}

void LibraryDrawer::emptyPeakSlots (const juce::StringArray& keys)
{
    // The Peak can't empty a slot; an empty slot holds the Init Patch (staged, NOT SENT, undoable).
    library.checkpoint();
    int n = 0;
    for (const auto& k : keys)
        if (const auto p = patchFor (k); p && p->location() && ! isEmpty (*p))
        {
            auto empty = emptyPatch();
            empty.setLocation (p->location());
            library.stagePeakSlot (empty);
            ++n;
        }
    setStatus (juce::String (n) + " slot(s) emptied: NOT SENT. Save all changes writes the Init Patch there.");
}

void LibraryDrawer::swapInPeak (const juce::StringArray& keys, peak::Patch::Location target)
{
    // Source slots (in drag order) and the block of target slots they go to.
    std::vector<peak::Patch::Location> src, dst;
    for (const auto& k : keys)
        if (const auto p = patchFor (k); p && target.slot + (int) dst.size() < 128)
        {
            src.push_back (*p->location());
            dst.push_back ({ target.bank, target.slot + (int) dst.size() });
        }
    if (src.empty() || src == dst)
        return;

    for (const auto& l : dst)
        if (! library.peakSlot (l))
            return setStatus (juce::String (l.name()) + " hasn't been read from the Peak yet, so it can't be swapped");

    // Read every source first, then stage, so overlapping blocks don't read their own writes.
    std::vector<std::pair<peak::Patch::Location, peak::Patch>> result;
    for (const auto& [to, from] : peak::Patch::planSwap (src, dst))
        result.emplace_back (to, *library.peakSlot (from));

    library.checkpoint();
    for (auto [loc, p] : result)
    {
        p.setLocation (loc);
        library.stagePeakSlot (p);
    }

    selection.clear();
    for (const auto& l : dst)
        selection.insert ("p:" + juce::String (l.bank) + ":" + juce::String (l.slot));
    expanded.insert ("pb:" + juce::String (target.bank));
    rebuildRows();

    const auto arrow = juce::String::fromUTF8 (" \xe2\x86\x94 ");
    setStatus ((src.size() == 1 ? "Swapped " + juce::String (src[0].name()) + arrow + juce::String (dst[0].name())
                                : "Moved " + juce::String ((int) src.size()) + " patches; the ones they replaced took their places")
               + ": NOT SENT. Save all changes writes them to the Peak.");
}

void LibraryDrawer::saveAllChanges()
{
    // An edited patch is saved back where it came from: My Banks at once (undoable), Peak
    // memory along with the other changes below.
    std::optional<peak::Patch> editedForPeak;
    if (homeEdited && homeKey.startsWith ("l:"))
    {
        library.checkpoint();
        library.replacePatch (homeKey.substring (2), model.getPatch());
        model.markSaved (std::nullopt, "My Banks: " + nameOf (model.getPatch()));
        setStatus ("Saved the edited \"" + nameOf (model.getPatch()) + "\" in My Banks");
    }
    else if (homeEdited && homeKey.startsWith ("p:"))
    {
        auto p = model.getPatch();
        p.setLocation (peak::Patch::Location { homeKey.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                               homeKey.fromLastOccurrenceOf (":", false, false).getIntValue() });
        editedForPeak = p;
    }

    auto pending = library.unsentPatches();
    if (editedForPeak)
    {
        std::erase_if (pending, [&] (const peak::Patch& p) { return p.location() == editedForPeak->location(); });
        pending.push_back (*editedForPeak);
        std::sort (pending.begin(), pending.end(), [] (const peak::Patch& a, const peak::Patch& b) {
            return a.location()->bank * 128 + a.location()->slot < b.location()->bank * 128 + b.location()->slot;
        });
    }
    if (pending.empty())
        return homeKey.startsWith ("l:") ? void() : setStatus ("Nothing to send");

    // List what gets overwritten, then which banks can be backed up first.
    juce::StringArray lines;
    std::set<int> banks;
    for (const auto& p : pending)
    {
        const auto loc = *p.location();
        banks.insert (loc.bank);
        if (lines.size() < 12)
        {
            const auto& old = library.syncedSlot (loc);
            lines.add (juce::String (loc.name()) + "  " + (old ? "\"" + nameOf (*old) + "\"" : juce::String ("(not read)")) + juce::String::fromUTF8 ("  \xe2\x86\x92  \"") + nameOf (p) + "\"");
        }
    }
    if ((int) pending.size() > lines.size())
        lines.add (juce::String::fromUTF8 ("\xe2\x80\xa6 and ") + juce::String ((int) pending.size() - lines.size()) + " more");

    juce::StringArray backedUp, notBackedUp;
    for (const auto b : banks)
        (library.knownSlots (b) == 128 ? backedUp : notBackedUp).add (bankLetter (b));

    auto message = "Write " + juce::String ((int) pending.size()) + " change(s) to the Peak?\n\n" + lines.joinIntoString ("\n") + "\n\n";
    if (! backedUp.isEmpty())
        message << "Bank " << backedUp.joinIntoString (", ") << " will be backed up first. ";
    if (! notBackedUp.isEmpty())
        message << "Bank " << notBackedUp.joinIntoString (", ") << " hasn't been fully read from the Peak, so it can't be backed up first "
                << "(to enable it, cancel and Fetch that bank).";

    juce::AlertWindow::showAsync (confirmOptions ("Save all changes", message, "Write to the Peak", this), [this, pending, banks, editedForPeak] (int res) {
        if (res != 1)
            return setStatus ("Nothing written; the changes are still NOT SENT");

        juce::StringArray backups;
        for (const auto b : banks)
            if (library.knownSlots (b) == 128)
            {
                juce::File f;
                if (const auto r = library.backupBank (b, &f); r.failed())
                    return setStatus ("Nothing written: backup failed (" + r.getErrorMessage() + ")");
                backups.add (f.getFileName());
            }

        auto& connection = model.getConnection();
        for (size_t i = 0; i < pending.size(); ++i)
        {
            if (! connection.sendSysex (pending[i].toSysex(), (double) i * msBetweenWrites))
                return setStatus ("Stopped: no MIDI output to the Peak (" + juce::String ((int) i) + " written)");
            library.storePeakSlot (pending[i]); // now on the synth: no longer NOT SENT
        }
        if (editedForPeak)
            model.markSaved (editedForPeak->location(), "Saved to " + juce::String (editedForPeak->location()->name()));
        const auto seconds = juce::roundToInt ((double) pending.size() * msBetweenWrites / 1000.0);

        // Once the writes have gone, read the banks back from the Peak to check every one landed.
        std::map<int, peak::Patch> written;
        for (const auto& p : pending)
            written[p.location()->bank * 128 + p.location()->slot] = p;
        juce::Timer::callAfterDelay ((int) ((double) pending.size() * msBetweenWrites) + 1500,
                                     [safe = juce::Component::SafePointer<LibraryDrawer> (this), banks, written] {
                                         if (safe != nullptr && safe->fetchQueue.empty())
                                             safe->startFetch (std::vector<int> (banks.begin(), banks.end()), written);
                                     });
        setStatus ("Writing " + juce::String ((int) pending.size()) + " change(s) to the Peak" + (seconds > 1 ? " (about " + juce::String (seconds) + " s)" : juce::String())
                   + (backups.isEmpty() ? juce::String() : ". Backup: " + backups.joinIntoString (", ")));
    });
}

void LibraryDrawer::storeEditorPatch (const juce::String& bank)
{
    library.checkpoint();
    bool dup = false;
    library.add (model.getPatch(), "Editor, " + today(), bank, &dup);
    expanded.insert ("lb:" + bank);
    rebuildRows();
    setStatus (dup ? "Already in " + bank : "Stored \"" + nameOf (model.getPatch()) + "\" in " + bank);
}

juce::PopupMenu LibraryDrawer::categoryMenu (const juce::String& key)
{
    juce::PopupMenu menu;
    const auto p = patchFor (key);
    const auto current = p ? p->get (*peak::findParam ("patch_category")) : -1;
    const auto& names = peak::categoryNames();
    for (int i = 0; i < (int) names.size(); ++i)
        menu.addItem (names[(size_t) i], true, i == current, [this, key, i] { applyCategory (key, i); });
    return menu;
}

void LibraryDrawer::chooseCategory (const juce::String& key)
{
    categoryMenu (key).showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void LibraryDrawer::applyCategory (const juce::String& key, int category)
{
    const auto& param = *peak::findParam ("patch_category");
    const auto name = juce::String (peak::formatValue (param, category));

    library.checkpoint();
    if (key.startsWith ("l:"))
    {
        // My Banks: applies to the whole selection if this row is part of it.
        auto keys = selectedKeys (libraryPane);
        if (! keys.contains (key))
            keys = { key };
        for (const auto& k : keys)
            library.setCategory (k.substring (2), category);
        setStatus ("Category " + name + ": " + juce::String (keys.size()) + " patch(es)");
        return;
    }

    // Peak memory: the category lives in the patch, so changing it rewrites the slot.
    if (auto p = patchFor (key))
    {
        if (p->get (param) == category)
            return;
        const auto where = *p->location();
        p->set (param, category);
        stageForPeak ({ *p }, where, "\"" + nameOf (*p) + "\" as " + name, false);
    }
}

void LibraryDrawer::renameOnPeak (const peak::Patch& patch)
{
    // Renaming a slot means writing it back with the new name (confirmed, bank backed up).
    askText ("Rename on the Peak", "New name for " + juce::String (patch.location()->name()) + " (16 characters):", nameOf (patch), [this, patch] (const juce::String& name) {
        auto renamed = patch;
        renamed.setName (name.substring (0, 16).toStdString());
        stageForPeak ({ renamed }, *patch.location(), "\"" + nameOf (renamed) + "\" (renamed)", false);
    });
}

void LibraryDrawer::sendBankToPeak (const juce::String& bank, int peakBank)
{
    std::vector<peak::Patch> patches;
    for (const auto* e : library.entriesIn (bank))
        patches.push_back (e->patch);
    stageForPeak (patches, { peakBank, 0 }, "\"" + bank + "\" (" + juce::String ((int) patches.size()) + " patches)");
}

void LibraryDrawer::copyPeakToLibrary()
{
    library.checkpoint(); // one undo step for all banks
    int banks = 0;
    for (int b = 0; b < 4; ++b)
    {
        juce::StringArray keys;
        for (int s = 0; s < 128; ++s)
            if (library.peakSlot ({ b, s }))
                keys.add ("p:" + juce::String (b) + ":" + juce::String (s));
        if (! keys.isEmpty())
        {
            addToBank (keys, "Peak Bank " + bankLetter (b) + " " + today(), -1, false);
            ++banks;
        }
    }
    if (banks == 0)
        setStatus ("Nothing to copy yet: Fetch the Peak's banks first");
}

void LibraryDrawer::fetchMenu()
{
    juce::PopupMenu menu;
    menu.addItem (juce::String::fromUTF8 ("All banks (A\xe2\x80\x93" "D)"), [this] { startFetch ({ 0, 1, 2, 3 }); });
    menu.addSeparator();
    for (int b = 0; b < 4; ++b)
        menu.addItem ("Bank " + bankLetter (b) + " (" + juce::String (library.knownSlots (b)) + "/128 read)", [this, b] { startFetch ({ b }); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fetchButton));
}

void LibraryDrawer::startFetch (const std::vector<int>& banks, std::map<int, peak::Patch> expected)
{
    fetchQueue.clear();
    fetchExpected = std::move (expected);
    fetchMismatches.clear();
    juce::StringArray letters;
    for (const auto b : banks)
    {
        letters.add (bankLetter (b));
        for (int s = 0; s < 128; ++s)
            fetchQueue.push_back ({ b, s });
    }
    fetchWhat = (banks.size() == 4 ? juce::String (juce::String::fromUTF8 ("banks A\xe2\x80\x93" "D")) : "bank " + letters.joinIntoString (", "));
    fetchIndex = 0;
    fetchRetries = fetchMissed = 0;
    fetchButton.setButtonText ("Stop");
    if (! syncCheck) // a quiet check leaves the banks as they are
        for (const auto b : banks)
            expanded.insert ("pb:" + juce::String (b));
    rebuildRows();
    fetchNext();
}

void LibraryDrawer::fetchNext()
{
    if (fetchIndex >= fetchQueue.size())
        return stopFetch ("Fetched");
    const auto where = fetchQueue[fetchIndex];
    if (! model.getConnection().sendSysex (peak::slotRequest (where.bank, where.slot)))
        return stopFetch ("Stopped: no MIDI output to the Peak.");
    fetchTimeout.startTimer (1500); // no answer: ask again (twice), then skip the slot
    if (where.slot % 8 == 0)
        setStatus ((syncCheck ? "Checking " : "Fetching ") + fetchWhat + " from the Peak: " + juce::String (where.name()) + " (" + juce::String ((int) fetchIndex + 1) + "/"
                   + juce::String ((int) fetchQueue.size()) + ")");
}

void LibraryDrawer::startSyncCheck()
{
    // Once per session: reopening a plugin's window doesn't repeat it.
    static bool checkedThisSession = false;
    if (checkedThisSession || ! fetchQueue.empty() || ! model.getConnection().isOutputOpen())
        return;
    checkedThisSession = true;
    for (int b = 0; b < 4; ++b)
    {
        syncBefore[(size_t) b].clear();
        for (int s = 0; s < 128; ++s)
            syncBefore[(size_t) b].push_back (library.syncedSlot ({ b, s }));
        syncChanged[(size_t) b].clear();
    }
    syncCheck = true;
    startFetch ({ 0, 1, 2, 3 });
    fetchButton.setButtonText ("Stop");
    setStatus ("Checking the Peak's banks against the editor's copy...");
}

void LibraryDrawer::finishSyncCheck()
{
    syncCheck = false;
    juce::StringArray what, backups;
    for (int b = 0; b < 4; ++b)
    {
        const auto n = syncChanged[(size_t) b].size();
        if (n == 0)
            continue;
        what.add ("Bank " + bankLetter (b) + " (" + juce::String (n) + " patch" + (n == 1 ? ")" : "es)"));
        // The editor's previous copy of the bank: possibly the only copy of what was overwritten.
        std::vector<peak::Patch> old;
        for (const auto& p : syncBefore[(size_t) b])
            if (p)
                old.push_back (*p);
        juce::File file;
        if (library.backupPatches (old, "bank-" + bankLetter (b) + "-before-sync", &file))
            backups.add (file.getFileName());
    }
    for (auto& v : syncBefore)
        v.clear();
    if (what.isEmpty())
        return setStatus ("The Peak's banks match the editor's copy");
    warnOutOfSync (what.joinIntoString (", "), backups);
}

void LibraryDrawer::warnOutOfSync (const juce::String& what, const juce::StringArray& backups)
{
    setStatus ("Peak banks changed outside the editor: " + what);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Peak banks changed outside the editor")
                                      .withMessage ("The Peak's memory no longer matched the editor's copy: " + what + ".\n\n"
                                                    "The Library now shows what the Peak holds. The editor's previous copy was saved to Backups"
                                                    + (backups.isEmpty() ? juce::String (".") : " (" + backups.joinIntoString (", ") + ").")
                                                    + (library.unsentCount() > 0 ? "\n\nNOT SENT changes are kept: check them before saving." : juce::String()))
                                      .withButton ("OK", 0)
                                      .withAssociatedComponent (this),
                                  [] (int) {});
}

void LibraryDrawer::stopFetch (const juce::String& why)
{
    fetchTimeout.stopTimer();
    if (syncCheck)
    {
        fetchQueue.clear();
        fetchIndex = 0;
        fetchButton.setButtonText ("Fetch");
        if (why == "Fetched")
            return finishSyncCheck();
        syncCheck = false; // stopped part-way: nothing to report
        for (auto& v : syncBefore)
            v.clear();
        return setStatus ("Stopped checking the Peak's banks");
    }
    const auto done = (int) fetchIndex - fetchMissed;
    fetchQueue.clear();
    fetchIndex = 0;
    fetchButton.setButtonText ("Fetch");
    const auto missed = fetchMissed > 0 ? ", " + juce::String (fetchMissed) + " didn't answer" : juce::String();
    if (! fetchExpected.empty())
    {
        // Read back after Save all changes.
        const auto n = (int) fetchExpected.size();
        setStatus (fetchMismatches.isEmpty() && fetchMissed == 0
                       ? "Checked " + fetchWhat + " on the Peak: all " + juce::String (n) + " change" + (n == 1 ? " is" : "s are") + " there"
                       : "Checked " + fetchWhat + " on the Peak: " + (fetchMismatches.isEmpty() ? juce::String()
                             : juce::String (fetchMismatches.size()) + " didn't match what was sent (" + fetchMismatches.joinIntoString (", ") + ")")
                             + missed + ". The library now shows what the Peak holds.");
        fetchExpected.clear();
        return;
    }
    setStatus (why + " " + fetchWhat + ": " + juce::String (done) + " patch" + (done == 1 ? "" : "es") + " read" + missed);
}

void LibraryDrawer::askText (const juce::String& titleText, const juce::String& prompt, const juce::String& initial, std::function<void (const juce::String&)> onOk, bool allowEmpty)
{
    auto* w = new juce::AlertWindow (titleText, prompt, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, onOk, allowEmpty] (int r) {
                            if (r == 1 && (allowEmpty || w->getTextEditorContents ("text").trim().isNotEmpty()))
                            {
                                library.checkpoint(); // every text prompt here changes the library
                                onOk (w->getTextEditorContents ("text"));
                            }
                        }),
                        true);
}

void LibraryDrawer::setAllExpanded (bool shouldExpand)
{
    expanded.clear();
    if (shouldExpand)
        for (int pane = 0; pane < 2; ++pane)
            for (const auto& r : rows (pane))
                if (r.isBank)
                    expanded.insert (r.key());
    rebuildRows();
}

bool LibraryDrawer::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".syx"))
            return true;
    return false;
}

void LibraryDrawer::filesDropped (const juce::StringArray& files, int, int)
{
    library.checkpoint();
    int added = 0, dups = 0;
    for (const auto& path : files)
    {
        const juce::File f (path);
        int d = 0;
        added += library.importFile (f, &d);
        dups += d;
        expanded.insert ("lb:" + f.getFileNameWithoutExtension());
    }
    rebuildRows();
    setStatus ("Imported " + juce::String (added) + " patch(es)" + (dups > 0 ? ", " + juce::String (dups) + " already there" : juce::String()));
}

void LibraryDrawer::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
}

juce::String LibraryDrawer::today() const
{
    return juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
}
