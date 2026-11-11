#pragma once

#include "peak/PeakModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The one path for overwriting a Peak memory slot: asks the user (naming the slot
// and the patch it replaces), backs up the bank first when its contents are known,
// then writes the editor's patch. `done` receives a status message.
namespace SlotWriter
{
void confirmAndWrite (PeakModel&, peak::Patch::Location, juce::Component* parent, std::function<void (const juce::String&)> done);
// The write itself, for callers that have already asked: backs up the bank if it can, then writes.
// Returns a status message; `ok` says whether the patch was written.
juce::String write (PeakModel&, peak::Patch::Location, bool& ok);
}
