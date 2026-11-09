#pragma once

#include "peak/PeakConnection.h"
#include "peak/core/Patch.h"

#include <map>

// The editor's view of the Peak's edit buffer, shared by every editor window in
// the process (use via juce::SharedResourcePointer). Message thread only.
// Keeps itself in sync with knob moves and patch dumps coming from the Peak, and
// sends user edits as throttled CC/NRPN.
class PeakModel final : private juce::ChangeListener,
                        private juce::Timer
{
public:
    PeakModel();
    ~PeakModel() override;

    PeakConnection& getConnection() { return *connection; }

    const peak::Patch& getPatch() const noexcept { return patch; }
    int get (const peak::ParamDef&) const;

    // A user edit: updates the model and sends it to the Peak (coalesced while dragging).
    void set (const peak::ParamDef&, int value);

    // Replaces the whole patch. Sending is explicit: it overwrites the Peak's edit buffer.
    void loadPatch (const peak::Patch&, const juce::String& source);
    bool sendPatchToPeak();

    // Where the current patch came from, e.g. "Received from Peak at 14:38".
    juce::String getSource() const { return source; }
    bool hasPatch() const noexcept { return loaded; }

    // The parameter most recently changed from either side, for the panel display.
    const peak::ParamDef* getLastTouched() const noexcept { return lastTouched; }
    void setLastTouched (const peak::ParamDef* p) { lastTouched = p; changed.sendChangeMessage(); }

    juce::ChangeBroadcaster changed;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    juce::SharedResourcePointer<PeakConnection> connection;
    peak::Patch patch;
    std::vector<int> unstoredValues; // parameters the dump doesn't hold (e.g. Animate Hold)
    std::map<int, int> pendingSends; // param index -> latest value
    juce::String source { "No patch loaded" };
    bool loaded = false;
    const peak::ParamDef* lastTouched = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakModel)
};
