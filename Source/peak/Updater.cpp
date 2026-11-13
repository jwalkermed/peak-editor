#include "Updater.h"

#include <juce_events/juce_events.h>

namespace
{
const juce::String bundleName (const juce::String& kind)
{
    return kind == "App" ? "Peak Editor.app" : kind == "AU" ? "Peak Editor.component" : "Peak Editor.vst3";
}

// Runs a command, waiting up to a minute; true if it exits with 0.
bool runCommand (const juce::StringArray& command)
{
    juce::ChildProcess process;
    return process.start (command, 0) && process.waitForProcessToFinish (60000) && process.getExitCode() == 0;
}

juce::String versionOf (const juce::File& bundle)
{
    juce::ChildProcess process;
    if (! process.start (juce::StringArray { "/usr/bin/plutil", "-extract", "CFBundleShortVersionString", "raw",
                                             bundle.getChildFile ("Contents/Info.plist").getFullPathName() }))
        return {};
    return process.readAllProcessOutput().trim();
}
} // namespace

std::vector<Updater::Part> Updater::parts()
{
    using F = juce::File;
    const auto home = F::getSpecialLocation (F::userHomeDirectory);
    std::vector<Part> out;

    // The app: the one that's running, if this is the app; else the one in Applications.
    auto app = F::getSpecialLocation (F::currentApplicationFile);
    if (! juce::JUCEApplicationBase::isStandaloneApp() || ! app.hasFileExtension ("app"))
        app = F ("/Applications/Peak Editor.app");
    out.push_back ({ "App", app });

    // The plugins: where they're installed (yours first, then the computer's), else your plug-in folders.
    for (const auto& [kind, folder] : { std::pair<juce::String, juce::String> { "AU", "Components" }, { "VST3", "VST3" } })
    {
        const auto mine = home.getChildFile ("Library/Audio/Plug-Ins/" + folder + "/" + bundleName (kind));
        const auto shared = F ("/Library/Audio/Plug-Ins/" + folder + "/" + bundleName (kind));
        out.push_back ({ kind, shared.isDirectory() && ! mine.isDirectory() ? shared : mine });
    }
    return out;
}

Updater::Updater (UpdateCheck::Release r, std::function<void (float, const juce::String&)> progressCallback,
                  std::function<void (bool, const juce::String&)> doneCallback)
    : juce::Thread ("Updater"), release (std::move (r)), onProgress (std::move (progressCallback)), onDone (std::move (doneCallback))
{
    startThread();
}

Updater::~Updater()
{
    stopThread (20000);
    cancelPendingUpdate();
}

void Updater::report (float p, const juce::String& s)
{
    {
        const juce::ScopedLock sl (lock);
        progress = p;
        status = s;
    }
    triggerAsyncUpdate();
}

void Updater::handleAsyncUpdate()
{
    float p;
    juce::String s, message;
    bool done, success;
    {
        const juce::ScopedLock sl (lock);
        p = progress;
        s = status;
        message = result;
        done = finished;
        success = ok;
    }
    if (onProgress)
        onProgress (p, s);
    if (done && onDone)
    {
        auto callback = std::exchange (onDone, nullptr); // once
        callback (success, message);
    }
}

bool Updater::download (const juce::URL& url, const juce::File& to, float from, float span)
{
    int status = 0;
    juce::StringPairArray headers;
    const auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                   .withConnectionTimeoutMs (15000)
                                                   .withNumRedirectsToFollow (5)
                                                   .withStatusCode (&status)
                                                   .withExtraHeaders ("User-Agent: Peak-Editor"));
    if (stream == nullptr || status >= 400)
        return false;
    juce::FileOutputStream out (to);
    if (! out.openedOk())
        return false;
    const auto total = stream->getTotalLength();
    juce::HeapBlock<char> buffer (65536);
    juce::int64 got = 0;
    while (! stream->isExhausted() && ! threadShouldExit())
    {
        const auto n = stream->read (buffer, 65536);
        if (n <= 0)
            break;
        out.write (buffer, (size_t) n);
        got += n;
        if (total > 0)
            report (from + span * (float) got / (float) total, "Downloading " + to.getFileName() + juce::String::fromUTF8 ("\xe2\x80\xa6"));
    }
    out.flush();
    return ! threadShouldExit() && got > 0 && (total <= 0 || got == total);
}

juce::String Updater::install (const Part& part, const juce::File& work, float from, float span)
{
    const auto wanted = release.version.trimCharactersAtStart ("vV");
    juce::String zipName;
    for (const auto& name : release.downloads.getAllKeys())
        if (name.endsWithIgnoreCase ("-macOS-" + part.kind + ".zip"))
            zipName = name;
    if (zipName.isEmpty())
        return "The release has no " + part.kind + " download.";

    const auto zip = work.getChildFile (zipName);
    if (! download (juce::URL (release.downloads[zipName]), zip, from, span * 0.8f))
        return threadShouldExit() ? juce::String ("Cancelled.") : "Couldn't download " + zipName + ".";

    report (from + span * 0.85f, juce::String::fromUTF8 ("Installing the ") + part.kind + juce::String::fromUTF8 ("\xe2\x80\xa6"));
    const auto unpacked = work.getChildFile (part.kind);
    if (! runCommand ({ "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), unpacked.getFullPathName() }))
        return "Couldn't unpack " + zipName + ".";
    const auto fresh = unpacked.getChildFile (bundleName (part.kind));
    if (! fresh.isDirectory() || versionOf (fresh) != wanted)
        return zipName + " isn't version " + wanted + ".";

    // Swap it in: the old one aside, the new one in its place (the old one back if that fails).
    const auto target = part.location;
    const auto aside = work.getChildFile ("old " + bundleName (part.kind));
    target.getParentDirectory().createDirectory();
    if (target.exists() && ! target.moveFileTo (aside))
        return "Couldn't replace " + target.getFullPathName() + " (no permission?).";
    if (! fresh.moveFileTo (target))
    {
        if (aside.exists())
            aside.moveFileTo (target);
        return "Couldn't install " + target.getFullPathName() + " (no permission?).";
    }
    runCommand ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", target.getFullPathName() });
    return {};
}

void Updater::run()
{
    const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("Peak Editor update", {}, false);
    work.createDirectory();
    const auto all = parts();
    juce::StringArray failed, done;
    for (size_t i = 0; i < all.size() && ! threadShouldExit(); ++i)
    {
        const auto span = 1.0f / (float) all.size();
        if (const auto problem = install (all[i], work, (float) i * span, span); problem.isNotEmpty())
            failed.add (problem);
        else
            done.add (all[i].kind);
    }
    if (done.contains ("AU"))
        runCommand ({ "/usr/bin/killall", "-9", "AudioComponentRegistrar" }); // macOS rescans the AUs
    work.deleteRecursively();

    {
        const juce::ScopedLock sl (lock);
        finished = true;
        ok = failed.isEmpty();
        result = failed.isEmpty() ? "Updated to " + release.version + "."
                                  : failed.joinIntoString ("\n") + (done.isEmpty() ? juce::String() : "\nUpdated: " + done.joinIntoString (", ") + ".");
        progress = 1.0f;
        status = {};
    }
    triggerAsyncUpdate();
}
