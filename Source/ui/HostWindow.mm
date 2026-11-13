#include "HostWindow.h"

#import <AppKit/AppKit.h>

namespace
{
NSWindow* windowOf (juce::Component& editor)
{
    if (auto* peer = editor.getPeer())
        if (auto* view = static_cast<NSView*> (peer->getNativeHandle()))
            return [view window];
    return nil;
}
} // namespace

namespace HostWindow
{
std::optional<juce::Point<int>> topLeft (juce::Component& editor)
{
    auto* window = windowOf (editor);
    if (window == nil || ! [window isVisible])
        return std::nullopt;
    const auto frame = [window frame];
    return juce::Point<int> ((int) frame.origin.x, (int) (frame.origin.y + frame.size.height));
}

bool moveTo (juce::Component& editor, juce::Point<int> point)
{
    auto* window = windowOf (editor);
    if (window == nil)
        return false;
    auto frame = [window frame];
    frame.origin = NSMakePoint (point.x, point.y - frame.size.height);
    // Only onto a screen: at least half of the window, and its title bar, must be visible on one.
    for (NSScreen* screen in [NSScreen screens])
    {
        const auto visible = [screen visibleFrame];
        const auto overlap = NSIntersectionRect (visible, frame);
        const auto titleBar = NSMakeRect (frame.origin.x, NSMaxY (frame) - 24, frame.size.width, 24);
        if (overlap.size.width * overlap.size.height >= 0.5 * frame.size.width * frame.size.height
            && NSIntersectsRect (visible, titleBar))
        {
            [window setFrameTopLeftPoint: NSMakePoint (point.x, point.y)];
            return true;
        }
    }
    return false;
}
} // namespace HostWindow
