#pragma once

#include "peak/Updater.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The Update window (a card in an Overlay), from the footer's "Update available": the new version and
// what's new, what will be updated, then Install Update (with progress), and afterwards Relaunch (the
// app) or a note to reopen the DAW project (a plugin).
class UpdateCard final : public juce::Component
{
public:
    enum class State { available, installing, installed, failed };

    explicit UpdateCard (UpdateCheck::Release);
    ~UpdateCard() override;

    std::function<void()> onClose;
    void show (State, float progress = 0.0f, const juce::String& message = {}); // also for snapshots

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void install();
    void relaunch();

    UpdateCheck::Release release;
    State state = State::available;
    float progress = 0.0f;
    juce::String message;
    std::unique_ptr<Updater> updater;

    juce::TextEditor notes;
    juce::TextButton laterButton { "Later" }, pageButton { "View on GitHub" }, installButton { "Install Update" },
        relaunchButton { "Relaunch Now" };
    juce::Rectangle<int> topArea, partsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateCard)
};
