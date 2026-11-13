#pragma once

#include "peak/UpdateCheck.h"

#include <functional>
#include <vector>

// Installs a newer release, as the installer script does but from inside the editor: downloads the App, AU
// and VST3 zips from the release, unpacks each (ditto), checks it is that version, and swaps it in where
// that part is installed now (the app that's running, ~/Library/Audio/Plug-Ins, ...; a part not installed
// anywhere goes where the installer puts it). A part that can't be swapped in is put back as it was. Then
// the quarantine flag is cleared and the AU list refreshed. Runs in the background; progress and the
// result arrive on the message thread.
class Updater final : private juce::Thread,
                      private juce::AsyncUpdater
{
public:
    struct Part
    {
        juce::String kind;   // "App", "AU" or "VST3" (as in the zips' names)
        juce::File location; // where it is (or will be) installed
    };
    static std::vector<Part> parts(); // what would be updated, and where

    Updater (UpdateCheck::Release, std::function<void (float progress, const juce::String& status)> onProgress,
             std::function<void (bool ok, const juce::String& message)> onDone);
    ~Updater() override;

private:
    void run() override;
    void handleAsyncUpdate() override;
    void report (float progress, const juce::String& status);
    bool download (const juce::URL&, const juce::File& to, float from, float span);
    juce::String install (const Part&, const juce::File& work, float from, float span); // empty if it worked

    UpdateCheck::Release release;
    std::function<void (float, const juce::String&)> onProgress;
    std::function<void (bool, const juce::String&)> onDone;

    juce::CriticalSection lock;
    float progress = 0.0f;
    juce::String status, result;
    bool finished = false, ok = false;
};
