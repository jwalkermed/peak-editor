#pragma once

#include "peak/PeakModel.h"
#include "ui/CardStyle.h"
#include "ui/PanelLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>

// The main editor view: a vector drawing of the Peak's front panel, laid out
// from the lens- and parallax-corrected reference photo (PanelLayout), with each
// control bound to the patch parameter it drives on the hardware.
class FrontPanel final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit FrontPanel (PeakModel&);
    ~FrontPanel() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Asks the editor to show one of its tabs ("Mod Matrix", "Settings").
    std::function<void (const juce::String& tab)> onShowTab;
    // Save: the Library opens as a picker for where the patch goes (see LibraryDrawer::beginSave).
    std::function<void()> onSave;

    // Patch -/+ and Bank -/+ (also the arrow keys): Peak banks A-D, then My Banks, as one list.
    void stepPatch (int delta);
    void stepBank (int delta);

    void showMessage (const juce::String&);
    void closeCard(); // closes an open menu page or the wavetable picker

    // Panel area in reference-photo pixels, including the wooden end cheeks.
    static juce::Rectangle<float> designBounds();
    // The PEAK lettering traced from the panel, right-aligned at `right`, letters `h` high from `top`
    // (about 5.6 h wide). Also used in the editor's header.
    static juce::Path peakLogo (float right, float top, float h);
    // A menu button's page ("Osc", "Animate", ...), or nullptr if it has none.
    static std::unique_ptr<juce::Component> createMenuPage (PeakModel&, const juce::String& menu);
    // The wavetable picker for oscillator 1-3 (waveBefore: its Wave before More was chosen).
    // previewWave: show that wavetable's notes card as if hovered (snapshots).
    static std::unique_ptr<juce::Component> createWavePicker (PeakModel&, int oscillator, int waveBefore, int previewWave = -1);

    void resized() override;

private:
    // Where a selector value lives: a patch parameter or panel-only UI state.
    struct Source
    {
        const peak::ParamDef* param = nullptr;
        int* ui = nullptr;
        bool valid() const { return param != nullptr || ui != nullptr; }
    };

    struct Alternative
    {
        const peak::ParamDef* param = nullptr;
        Source when;
        int whenValue = 0;
    };

    struct Control
    {
        const PanelElement* e = nullptr;
        std::vector<Alternative> alternatives; // knob/fader/toggle: param chosen by a selector
        Source source;                         // led: lit when source == e->value; cycle button: stepped
        bool cycle = false;
        juce::String menu, action;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Source parseSource (const juce::String&);
    int read (const Source&) const;
    const peak::ParamDef* resolve (const Control&) const;
    Control* controlAt (juce::Point<float> designPoint);
    juce::Point<float> toDesign (juce::Point<float>) const;
    juce::AffineTransform designToScreen() const;

    void setParam (const peak::ParamDef&, int value);
    void press (Control&);
    void openMenu (const juce::String& name, const PanelElement& from);
    void stepLastTouched (int delta);
    void goTo (int bank, int index);

    bool animateHold = false;                          // the Hold button: Animate presses latch
    const peak::ParamDef* animatePressed = nullptr;    // released on mouse up unless Hold is on
    void movePage (int delta);

    void drawBackground (juce::Graphics&) const;
    void drawKnob (juce::Graphics&, const Control&) const;
    void drawButton (juce::Graphics&, const Control&) const;
    void drawLed (juce::Graphics&, const Control&) const;
    void drawFader (juce::Graphics&, const Control&) const;
    void drawDisplay (juce::Graphics&, const PanelElement&) const;

    PeakModel& model;
    std::vector<Control> controls;
    std::deque<PanelElement> changedElements; // the editor's departures from the hardware layout
    int modEnvSelect = 0;
    juce::String openMenuName, message;

    // The screen shows a touched parameter for a moment, then fades back to the patch.
    void timerCallback() override;
    float parameterAlpha() const;
    mutable juce::Rectangle<float> starArea, nameArea, categoryArea; // on the screen's patch view, in design coordinates
    void toggleFavourite();
    void chooseCategory();
    // The envelope or filter graph for the parameter on screen; false if it has none.
    bool drawGraph (juce::Graphics&, juce::Rectangle<float> inner, float lineHeight, float alpha) const;
    void setCategoryAndSave (int category);
    void showCard (std::unique_ptr<juce::Component>); // a menu page or the wavetable picker, over the panel
    std::unique_ptr<CardStyle::Overlay> overlay;
    void showHelp();
    static juce::Rectangle<float> helpButtonArea();
    void renamePatch(); // double-click on the name: rename, then overwrite or save elsewhere
    const peak::ParamDef* shownParam = nullptr;
    int shownValue = -1;
    double shownSince = 0.0; // Time::getMillisecondCounterHiRes

    Control* dragging = nullptr;
    const peak::ParamDef* dragParam = nullptr;
    int dragStartValue = 0;
    float dragStartY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrontPanel)
};
