#pragma once

#include "peak/PeakModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The Peak's mod matrix, in the style of the TEO-5 editor's drawer: 16 slots of
// Source A x Source B -> Depth -> Destination, then the 4 FX mod slots. Edits go to
// the Peak as NRPN like any knob. Click a slot's LED to turn it off (depth to 0,
// remembered) or back on; drag a depth, double-click restores the patch's value;
// right-click to clear or restore a slot. Clear all / Revert / Undo / Redo.
class ModMatrixDrawer final : public juce::Component,
                              private juce::ChangeListener
{
public:
    explicit ModMatrixDrawer (PeakModel&);
    ~ModMatrixDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override; // Cmd+Z / Shift+Cmd+Z

    static constexpr int numSlots = 16, numFxSlots = 4;

    // The parameters of one slot (main and FX slots both have two sources).
    struct Slot
    {
        juce::String label; // "1".."16", "FX 1".."FX 4"
        const peak::ParamDef* sourceA = nullptr;
        const peak::ParamDef* sourceB = nullptr;
        const peak::ParamDef* depth = nullptr;
        const peak::ParamDef* destination = nullptr;
        const std::vector<std::string>* sources = nullptr;
        const std::vector<std::string>* destinations = nullptr;
    };

    // Called by a row just before an edit made here, so each edit is one undo step.
    void userEdit (const juce::String& what);
    void setHint (const juce::String&);
    // Outlines a slot (0-15 main, 16-19 FX) and shows `message` in the hint line, e.g. after a
    // right-click on a panel control added it. Editing another slot or leaving the tab clears it.
    void focusSlot (int row, const juce::String& message);
    int getFocusedRow() const noexcept { return focusedRow; }
    void visibilityChanged() override;
    PeakModel& getModel() { return model; }

private:
    class AmountBar;
    class SlotRow;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    // Undo history: every mod parameter's value, before each edit made here.
    using State = std::vector<int>;
    State capture() const;
    void apply (const State&);
    void undoRedo (bool undo);
    void clearAll();
    void revertAll();
    void updateButtons();

    PeakModel& model;
    std::vector<Slot> slots;
    juce::OwnedArray<SlotRow> rows;
    std::vector<std::pair<State, juce::String>> undoStack, redoStack;
    juce::String seenSource; // the loaded patch: a new one starts the undo history again

    juce::Label title { {}, "MOD MATRIX" }, fxTitle { {}, "FX MOD MATRIX" }, summary, hint;
    juce::TextButton clearAllButton { "Clear all" }, revertButton { "Revert" }, undoButton { "Undo" }, redoButton { "Redo" };
    juce::String defaultHint;
    int focusedRow = -1;
    void clearFocus();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModMatrixDrawer)
};
