#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// The window a plugin editor sits in belongs to the DAW, which places it (often at the top left every
// time). On macOS the editor can still find that window and move it, so it can come back where it was.
// Positions are the window's top-left corner in screen points (macOS's own coordinates).
namespace HostWindow
{
std::optional<juce::Point<int>> topLeft (juce::Component& editor);
// Moves it, only if the window would still be mostly on a screen; true if it moved.
bool moveTo (juce::Component& editor, juce::Point<int> topLeft);
} // namespace HostWindow
