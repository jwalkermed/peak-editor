#include "ModMatrixDrawer.h"

#include "peak/core/ParamFormat.h"
#include "ui/DrawerStyle.h"

namespace
{
using namespace DrawerStyle;

constexpr int depthCentre = 64; // raw 0-127, shown as -64..+63
constexpr int slotRowHeight = 28;

const peak::ParamDef* param (const juce::String& id)
{
    return peak::findParam (id.toStdString());
}

// Drop-downs and buttons in the drawer lettering (Exo 2), as in the TEO-5 drawer.
struct DrawerLookAndFeel : juce::LookAndFeel_V4
{
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (11.0f); }
    juce::Font getPopupMenuFont() override { return font (12.0f); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? juce::Colour (0xff26282d).brighter (0.08f) : juce::Colour (0xff26282d));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (highlighted && b.isEnabled() ? juce::Colour (0xff4a4f59) : juce::Colour (0xff34373e));
        g.drawRoundedRectangle (r, 2.5f, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setColour (silkscreen.withAlpha (b.isEnabled() ? 1.0f : 0.35f));
        g.setFont (font (11.0f));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }
};

DrawerLookAndFeel& drawerLookAndFeel()
{
    static DrawerLookAndFeel lf;
    return lf;
}
} // namespace

//==============================================================================
// Depth: a bipolar bar filled from the centre.
class ModMatrixDrawer::AmountBar final : public juce::Slider
{
public:
    AmountBar() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setRange (0, 127, 1);
        setMouseDragSensitivity (300);
        setVelocityBasedMode (false);
        setScrollWheelEnabled (false); // drag only: the wheel scrolls the matrix instead
    }

    bool dimmed = false;
    std::optional<int> ghost;            // slot turned off: the depth it comes back with (dashed outline)
    std::function<int()> storedValue;    // double-click: the depth as saved in the patch
    std::function<void()> onContextMenu; // right-click: the slot's menu
    std::function<void()> onEditStart;   // just before a drag or double-click changes the depth

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu)
                onContextMenu();
            return;
        }
        if (onEditStart)
            onEditStart();
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Slider::mouseDrag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Slider::mouseUp (e);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onEditStart)
            onEditStart();
        setValue (storedValue ? storedValue() : depthCentre, juce::sendNotificationSync);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f, 3.5f);
        g.setColour (field);
        g.fillRoundedRectangle (b, 2.0f);
        g.setColour (pinstripe.withAlpha (0.45f));
        g.drawRoundedRectangle (b, 2.0f, 1.0f);

        const auto xFor = [&] (double v) { return b.getX() + b.getWidth() * (float) (v / 127.0); };
        const float centreX = xFor (depthCentre), valueX = xFor (getValue());
        g.setColour (dimmed ? silkscreen.withAlpha (0.25f) : accent.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centreX, valueX), b.getY() + 2.0f, juce::jmax (centreX, valueX), b.getBottom() - 2.0f));

        if (ghost.has_value())
        {
            const float ghostX = xFor (*ghost);
            juce::Path p;
            p.addRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centreX, ghostX), b.getY() + 2.0f, juce::jmax (centreX, ghostX), b.getBottom() - 2.0f));
            const float dashes[] = { 3.0f, 2.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (p, p, dashes, 2);
            g.setColour (accent.withAlpha (0.55f));
            g.fillPath (p);
            g.setFont (font (8.5f));
            g.drawText ("OFF", b.reduced (6.0f, 0.0f), *ghost < depthCentre ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
            return;
        }

        g.setColour (silkscreen.withAlpha (0.35f));
        g.fillRect (centreX - 0.5f, b.getY() + 1.0f, 1.0f, b.getHeight() - 2.0f);

        const int v = juce::roundToInt (getValue()) - depthCentre;
        g.setColour (silkscreen.withAlpha (dimmed ? 0.4f : 1.0f));
        g.setFont (font (9.5f));
        g.drawText (v > 0 ? "+" + juce::String (v) : juce::String (v), b.reduced (6.0f, 0.0f),
                    v < 0 ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
    }
};

//==============================================================================
class ModMatrixDrawer::SlotRow final : public juce::Component
{
public:
    SlotRow (ModMatrixDrawer& o, const Slot& s, int i) : owner (o), slot (s), index (i)
    {
        auto& peakModel = owner.getModel();
        for (const auto& n : *slot.sources)
        {
            sourceA.addItem (n, sourceA.getNumItems() + 1);
            sourceB.addItem (n, sourceB.getNumItems() + 1);
        }
        for (const auto& n : *slot.destinations)
            destination.addItem (n, destination.getNumItems() + 1);

        const auto bindCombo = [this, &peakModel] (juce::ComboBox& box, const peak::ParamDef* def, const char* what) {
            box.onChange = [this, &box, def, what, &peakModel] {
                if (updating || def == nullptr)
                    return;
                owner.userEdit (juce::String (what) + " of slot " + slot.label);
                peakModel.set (*def, box.getSelectedId() - 1);
            };
            box.setColour (juce::ComboBox::backgroundColourId, field);
            box.setColour (juce::ComboBox::textColourId, silkscreen);
            box.setColour (juce::ComboBox::outlineColourId, pinstripe.withAlpha (0.45f));
            box.setColour (juce::ComboBox::arrowColourId, silkscreen.withAlpha (0.6f));
            box.setLookAndFeel (&drawerLookAndFeel());
            addAndMakeVisible (box);
        };
        bindCombo (sourceA, slot.sourceA, "source A");
        if (slot.sourceB != nullptr)
            bindCombo (sourceB, slot.sourceB, "source B");
        bindCombo (destination, slot.destination, "destination");

        amount.storedValue = [this, &peakModel] { return peakModel.getStored (*slot.depth); };
        amount.onEditStart = [this] { owner.userEdit ("depth of slot " + slot.label); };
        amount.onContextMenu = [this] { showMenu(); };
        amount.onValueChange = [this, &peakModel] {
            if (! updating)
                peakModel.set (*slot.depth, (int) amount.getValue());
        };
        addAndMakeVisible (amount);
    }

    ~SlotRow() override
    {
        for (auto* box : { &sourceA, &sourceB, &destination })
            box->setLookAndFeel (nullptr);
    }

    bool isActive() const { return owner.getModel().get (*slot.depth) != depthCentre; }

    void refresh()
    {
        auto& peakModel = owner.getModel();
        const juce::ScopedValueSetter<bool> svs (updating, true);
        sourceA.setSelectedId (peakModel.get (*slot.sourceA) + 1, juce::dontSendNotification);
        if (slot.sourceB != nullptr)
            sourceB.setSelectedId (peakModel.get (*slot.sourceB) + 1, juce::dontSendNotification);
        destination.setSelectedId (peakModel.get (*slot.destination) + 1, juce::dontSendNotification);
        amount.setValue (peakModel.get (*slot.depth), juce::dontSendNotification);

        const bool active = isActive();
        if (remembered && active) // turned back on some other way: forget the remembered depth
            remembered.reset();
        amount.ghost = active ? std::nullopt : remembered;
        amount.dimmed = ! active;
        for (auto* box : { &sourceA, &sourceB, &destination })
            box->setColour (juce::ComboBox::textColourId, silkscreen.withAlpha (active ? 1.0f : 0.45f));
        wasActive = active;
        repaint();
        amount.repaint();
    }

    // The LED: turns a slot off (depth to 0, remembering it) and back on. The Peak has no
    // per-slot switch, so "off" is a depth of 0; the remembered depth is kept by the editor.
    void toggle()
    {
        auto& peakModel = owner.getModel();
        owner.userEdit ("slot " + slot.label + (isActive() ? " off" : " on"));
        if (isActive())
        {
            remembered = peakModel.get (*slot.depth);
            peakModel.set (*slot.depth, depthCentre);
            return;
        }
        const auto back = remembered.value_or (peakModel.getStored (*slot.depth));
        remembered.reset();
        if (back != depthCentre)
            peakModel.set (*slot.depth, back);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showMenu();
        else if (e.x < numberWidth)
            toggle();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.x < numberWidth ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void showMenu()
    {
        auto& peakModel = owner.getModel();
        const auto stored = [&] (const peak::ParamDef* d) { return d == nullptr || peakModel.get (*d) == peakModel.getStored (*d); };
        const bool changed = ! (stored (slot.sourceA) && stored (slot.sourceB) && stored (slot.depth) && stored (slot.destination));

        juce::PopupMenu m;
        m.addItem ("Clear depth", [this, &peakModel] {
            owner.userEdit ("clear depth of slot " + slot.label);
            peakModel.set (*slot.depth, depthCentre);
        });
        m.addItem ("Clear slot " + slot.label, [this, &peakModel] {
            owner.userEdit ("clear slot " + slot.label);
            remembered.reset();
            for (const auto* d : { slot.sourceA, slot.sourceB, slot.destination })
                if (d != nullptr)
                    peakModel.set (*d, 0);
            peakModel.set (*slot.depth, depthCentre);
        });
        m.addSeparator();
        m.addItem ("Restore slot " + slot.label + " to patch default", changed, false, [this, &peakModel] {
            owner.userEdit ("restore slot " + slot.label);
            remembered.reset();
            for (const auto* d : { slot.sourceA, slot.sourceB, slot.depth, slot.destination })
                if (d != nullptr)
                    peakModel.set (*d, peakModel.getStored (*d));
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (index % 2 ? rowA : rowB);
        const auto r = getLocalBounds().removeFromLeft (numberWidth).toFloat();
        const juce::Point<float> led (r.getX() + 9.0f, r.getCentreY());
        if (wasActive)
        {
            g.setColour (accent.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (led));
        }
        g.setColour (wasActive ? accent : juce::Colour (0xff3a2a1a));
        g.fillEllipse (juce::Rectangle<float> (6.5f, 6.5f).withCentre (led));
        if (! wasActive && remembered)
        {
            g.setColour (accent.withAlpha (0.5f)); // off with a depth to come back to: the LED can be clicked
            g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (led), 1.0f);
        }
        g.setColour (silkscreen.withAlpha (wasActive ? 1.0f : 0.5f));
        g.setFont (font (10.0f));
        g.drawText (slot.label, r.withTrimmedLeft (17.0f), juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        r.removeFromLeft (numberWidth);
        sourceA.setBounds (r.removeFromLeft (sourceWidth));
        r.removeFromLeft (gap);
        sourceB.setBounds (r.removeFromLeft (sourceWidth));
        sourceB.setVisible (slot.sourceB != nullptr);
        r.removeFromLeft (gap);
        destination.setBounds (r.removeFromRight (destWidth));
        r.removeFromRight (gap);
        amount.setBounds (r);
    }

    static constexpr int numberWidth = 52, sourceWidth = 118, destWidth = 140, gap = 6;

private:
    ModMatrixDrawer& owner;
    const Slot slot;
    const int index;
    bool updating = false, wasActive = false;
    std::optional<int> remembered;
    juce::ComboBox sourceA, sourceB, destination;
    AmountBar amount;
};

//==============================================================================
ModMatrixDrawer::ModMatrixDrawer (PeakModel& m) : model (m)
{
    for (int n = 1; n <= numSlots; ++n)
    {
        const auto p = "mod" + juce::String (n) + "_";
        slots.push_back ({ juce::String (n), param (p + "source1"), param (p + "source2"), param (p + "depth"), param (p + "dest"),
                           &peak::modSourceNames(), &peak::modDestinationNames() });
    }
    for (int n = 1; n <= numFxSlots; ++n)
    {
        const auto p = "fxmod" + juce::String (n) + "_";
        slots.push_back ({ "FX " + juce::String (n), param (p + "source"), param (p + "source2"), param (p + "depth"), param (p + "dest"),
                           &peak::fxSourceNames(), &peak::fxDestinationNames() });
    }
    for (size_t i = 0; i < slots.size(); ++i)
        addAndMakeVisible (rows.add (new SlotRow (*this, slots[i], (int) i)));

    for (auto* l : { &title, &fxTitle })
    {
        l->setFont (font (14.0f));
        l->setColour (juce::Label::textColourId, silkscreen);
        addAndMakeVisible (l);
    }
    summary.setFont (font (9.5f));
    summary.setColour (juce::Label::textColourId, silkscreen.withAlpha (0.75f));
    summary.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (summary);

    defaultHint = "Click an LED to turn a slot off or on. Drag a depth; double-click restores it. Right-click a depth "
                  "or slot to clear it or restore it. Each slot multiplies Source A by Source B.";
    hint.setText (defaultHint, juce::dontSendNotification);
    hint.setFont (juce::FontOptions (13.0f));
    hint.setColour (juce::Label::textColourId, silkscreen.withAlpha (0.65f));
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);

    for (auto* b : { &clearAllButton, &revertButton, &undoButton, &redoButton })
    {
        b->setLookAndFeel (&drawerLookAndFeel());
        addAndMakeVisible (b);
    }
    clearAllButton.setTooltip ("Every slot: sources Direct, depth 0 (one undo step)");
    revertButton.setTooltip ("Every slot as stored in the loaded patch (one undo step)");
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Shift+Cmd+Z)");
    clearAllButton.onClick = [this] { clearAll(); };
    revertButton.onClick = [this] { revertAll(); };
    undoButton.onClick = [this] { undoRedo (true); };
    redoButton.onClick = [this] { undoRedo (false); };

    setWantsKeyboardFocus (true);
    model.changed.addChangeListener (this);
    refresh();
}

ModMatrixDrawer::~ModMatrixDrawer()
{
    model.changed.removeChangeListener (this);
    for (auto* b : { &clearAllButton, &revertButton, &undoButton, &redoButton })
        b->setLookAndFeel (nullptr);
}

void ModMatrixDrawer::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (pinstripe);
    g.fillRect (12, 34, getWidth() - 24, 1);

    // Column headings above the first slot.
    if (rows.isEmpty())
        return;
    const auto first = rows.getFirst()->getBounds();
    g.setColour (silkscreen.withAlpha (0.55f));
    g.setFont (font (8.5f));
    auto h = juce::Rectangle<int> (first.getX(), first.getY() - 16, first.getWidth(), 14);
    g.drawText ("SLOT", h.removeFromLeft (SlotRow::numberWidth), juce::Justification::centredLeft);
    g.drawText ("SOURCE A", h.removeFromLeft (SlotRow::sourceWidth + SlotRow::gap), juce::Justification::centredLeft);
    g.drawText (juce::String::fromUTF8 ("\xc3\x97 SOURCE B"), h.removeFromLeft (SlotRow::sourceWidth + SlotRow::gap), juce::Justification::centredLeft);
    g.drawText ("DESTINATION", h.removeFromRight (SlotRow::destWidth), juce::Justification::centredLeft);
    g.drawText ("DEPTH", h, juce::Justification::centred);

    g.setColour (pinstripe.withAlpha (0.6f));
    g.fillRect (fxTitle.getX(), fxTitle.getY() - 4, getWidth() - 2 * fxTitle.getX(), 1);
}

void ModMatrixDrawer::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (24);
    title.setBounds (top.removeFromLeft (120));
    for (auto* b : { &clearAllButton, &revertButton, &undoButton, &redoButton })
    {
        b->setBounds (top.removeFromLeft (76).reduced (2, 1));
        top.removeFromLeft (4);
    }
    summary.setBounds (top);

    r.removeFromTop (26); // column headings
    hint.setBounds (r.removeFromBottom (40));
    for (int i = 0; i < numSlots; ++i)
        rows[i]->setBounds (r.removeFromTop (slotRowHeight));
    r.removeFromTop (12);
    fxTitle.setBounds (r.removeFromTop (22));
    for (int i = numSlots; i < rows.size(); ++i)
        rows[i]->setBounds (r.removeFromTop (slotRowHeight));
}

bool ModMatrixDrawer::keyPressed (const juce::KeyPress& key)
{
    const auto undo = key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0);
    const auto redo = key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    if (undo || redo)
        undoRedo (undo);
    return undo || redo;
}

void ModMatrixDrawer::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void ModMatrixDrawer::refresh()
{
    // Another patch loaded: its matrix starts a new undo history.
    if (const auto source = model.getSource(); source != seenSource)
    {
        seenSource = source;
        undoStack.clear();
        redoStack.clear();
    }

    int active = 0;
    for (int i = 0; i < rows.size(); ++i)
    {
        rows[i]->refresh();
        if (i < numSlots && rows[i]->isActive())
            ++active;
    }
    summary.setText (juce::String (active) + " of " + juce::String (numSlots) + " slots active", juce::dontSendNotification);
    updateButtons();
}

ModMatrixDrawer::State ModMatrixDrawer::capture() const
{
    State s;
    for (const auto& slot : slots)
        for (const auto* d : { slot.sourceA, slot.sourceB, slot.depth, slot.destination })
            s.push_back (d != nullptr ? model.get (*d) : 0);
    return s;
}

void ModMatrixDrawer::apply (const State& s)
{
    size_t i = 0;
    for (const auto& slot : slots)
        for (const auto* d : { slot.sourceA, slot.sourceB, slot.depth, slot.destination })
        {
            if (d != nullptr)
                model.set (*d, s[i]);
            ++i;
        }
}

void ModMatrixDrawer::userEdit (const juce::String& what)
{
    undoStack.push_back ({ capture(), what });
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
    setHint ({});
    updateButtons();
}

void ModMatrixDrawer::undoRedo (bool undo)
{
    auto& from = undo ? undoStack : redoStack;
    auto& to = undo ? redoStack : undoStack;
    if (from.empty())
        return;
    auto step = from.back();
    from.pop_back();
    to.push_back ({ capture(), step.second });
    apply (step.first);
    setHint ((undo ? "Undone: " : "Redone: ") + step.second);
    updateButtons();
}

void ModMatrixDrawer::clearAll()
{
    userEdit ("clear all");
    for (const auto& slot : slots)
    {
        for (const auto* d : { slot.sourceA, slot.sourceB, slot.destination })
            if (d != nullptr)
                model.set (*d, 0);
        model.set (*slot.depth, depthCentre);
    }
}

void ModMatrixDrawer::revertAll()
{
    userEdit ("revert");
    for (const auto& slot : slots)
        for (const auto* d : { slot.sourceA, slot.sourceB, slot.depth, slot.destination })
            if (d != nullptr)
                model.set (*d, model.getStored (*d));
}

void ModMatrixDrawer::updateButtons()
{
    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
    revertButton.setEnabled (model.hasPatch());
}

void ModMatrixDrawer::setHint (const juce::String& text)
{
    hint.setText (text.isEmpty() ? defaultHint : text, juce::dontSendNotification);
}
