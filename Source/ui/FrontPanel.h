#pragma once

#include "peak/PeakModel.h"
#include "ui/PanelLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The main editor view: a vector drawing of the Peak's front panel, laid out
// from the lens- and parallax-corrected reference photo (PanelLayout), with each
// control bound to the patch parameter it drives on the hardware.
class FrontPanel final : public juce::Component,
                         private juce::ChangeListener
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

    // Panel area in reference-photo pixels, including the wooden end cheeks.
    static juce::Rectangle<float> designBounds();

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
    void movePage (int delta);
    void showMessage (const juce::String&);

    void drawBackground (juce::Graphics&) const;
    void drawKnob (juce::Graphics&, const Control&) const;
    void drawButton (juce::Graphics&, const Control&) const;
    void drawLed (juce::Graphics&, const Control&) const;
    void drawFader (juce::Graphics&, const Control&) const;
    void drawDisplay (juce::Graphics&, const PanelElement&) const;

    PeakModel& model;
    std::vector<Control> controls;
    int modEnvSelect = 0;
    juce::String openMenuName, message;

    Control* dragging = nullptr;
    const peak::ParamDef* dragParam = nullptr;
    int dragStartValue = 0;
    float dragStartY = 0;
    int patchStepsApplied = 0; // patch encoder drags step through Peak memory

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrontPanel)
};
