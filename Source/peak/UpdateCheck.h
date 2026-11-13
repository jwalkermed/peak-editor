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
    static constexpr const char* releasesPage = "https://github.com/jwalkermed/peak-editor/releases/latest";

    // A newer release: its version ("v1.4.0"), page, notes (Markdown) and downloads (file name -> URL).
    struct Release
    {
        juce::String version, notes;
        juce::URL page;
        juce::StringPairArray downloads;
    };

    UpdateCheck() : juce::Thread ("Update check") { startThread (juce::Thread::Priority::low); }
    ~UpdateCheck() override
    {
        stopThread (5000);
        cancelPendingUpdate();
    }

    // The newer release, once found (message thread); empty version until then.
    const Release& getNewerRelease() const { return newer; }
    juce::String getNewerVersion() const { return newer.version; }
    juce::URL getReleasePage() const { return newer.page; }
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
        const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
        const auto tag = json["tag_name"].toString();
        if (! peak::isNewerVersion (tag.toStdString(), JucePlugin_VersionString))
            return;

        Release release;
        release.version = tag;
        release.notes = json["body"].toString();
        const auto page = json["html_url"].toString();
        release.page = juce::URL (page.startsWith ("https://github.com/") ? page : juce::String (releasesPage));
        if (const auto* assets = json["assets"].getArray())
            for (const auto& asset : *assets)
                if (const auto url = asset["browser_download_url"].toString(); url.startsWith ("https://github.com/"))
                    release.downloads.set (asset["name"].toString(), url);
        const juce::ScopedLock lock (foundLock);
        fromThread = std::move (release);
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        {
            const juce::ScopedLock lock (foundLock);
            newer = fromThread;
        }
        found.sendChangeMessage();
    }

    juce::CriticalSection foundLock;
    Release fromThread; // from the thread, before the async update
    Release newer;
};
