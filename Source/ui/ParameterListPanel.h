#pragma once

#include "peak/PeakModel.h"
#include "ui/ParamRows.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Phase 2 generic editor: every parameter from the table as a labelled slider,
// grouped by section, kept in two-way sync with the Peak via PeakModel.
// The faithful front-panel view (Phase 4) will replace this as the main view.
class ParameterListPanel final : public juce::Component,
                                 private juce::ChangeListener
{
public:
    explicit ParameterListPanel (PeakModel&);
    ~ParameterListPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void importSyx();
    void exportSyx();

    PeakModel& model;
    juce::Label patchLabel, hintLabel;
    juce::TextButton sendButton { "Send patch to Peak" }, importButton { juce::String::fromUTF8 ("Import .syx…") }, exportButton { juce::String::fromUTF8 ("Export .syx…") };
    juce::Viewport viewport;
    std::unique_ptr<ParamRows> content;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterListPanel)
};
