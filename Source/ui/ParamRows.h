#pragma once

#include "peak/PeakModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>

// Labelled sliders for a set of parameters, grouped under section headers and
// flowed into columns. Used by the Parameters tab and the menu pop-overs.
class ParamRows final : public juce::Component
{
public:
    ParamRows (PeakModel&, const std::vector<const peak::ParamDef*>& params);

    void refresh(); // pull values from the model
    void layout (int width, int columnWidth = 380);

private:
    struct Row
    {
        const peak::ParamDef* def = nullptr;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
    };

    struct Item
    {
        juce::Label* header;
        Row* row;
    };

    PeakModel& model;
    std::vector<std::unique_ptr<juce::Label>> headers;
    std::deque<Row> rows; // deque: stable addresses for items
    std::vector<Item> items;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamRows)
};
