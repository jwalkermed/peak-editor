#pragma once

#include "peak/core/Version.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// Whether a newer release of the editor is out: asks GitHub for the latest release once (a few seconds
// after starting, in the background), shared by every editor in the process (use via
// juce::SharedResourcePointer). Nothing happens if there's no network, or no newer release.
class UpdateCheck final : private juce::Thread,
                          private juce::AsyncUpdater
{
public:
    static constexpr const char* latestRelease = "https://api.github.com/repos/jwalkermed/peak-editor/releases/latest";

    UpdateCheck() : juce::Thread ("Update check") { startThread (juce::Thread::Priority::low); }
    ~UpdateCheck() override
    {
        stopThread (5000);
        cancelPendingUpdate();
    }

    // The newer release's version ("v1.3.0") and page, once found (message thread).
    juce::String getNewerVersion() const { return newerVersion; }
    juce::URL getReleasePage() const { return releasePage; }
    juce::ChangeBroadcaster found;

private:
    void run() override
    {
        if (wait (4000) || threadShouldExit())
            return; // let the editor start first
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (8000)
                                 .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: Peak-Editor");
        const auto stream = juce::URL (latestRelease).createInputStream (options);
        if (stream == nullptr || threadShouldExit())
            return;
        const auto release = juce::JSON::parse (stream->readEntireStreamAsString());
        const auto tag = release["tag_name"].toString();
        if (! peak::isNewerVersion (tag.toStdString(), JucePlugin_VersionString))
            return;
        tagFound = tag;
        pageFound = release["html_url"].toString();
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        newerVersion = tagFound;
        releasePage = juce::URL (pageFound.startsWith ("https://github.com/") ? pageFound : juce::String ("https://github.com/jwalkermed/peak-editor/releases/latest"));
        found.sendChangeMessage();
    }

    juce::String tagFound, pageFound; // from the thread, before the async update
    juce::String newerVersion;
    juce::URL releasePage;
};
