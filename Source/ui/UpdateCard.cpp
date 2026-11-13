#include "UpdateCard.h"

#include "ui/CardStyle.h"

namespace
{
using namespace CardStyle;

constexpr int cardWidth = 600, cardHeight = 460, pad = 22;

// The release notes as plain text: Markdown's #, ** and backticks dropped, links' text kept.
juce::String plainNotes (const juce::String& markdown)
{
    juce::StringArray lines;
    for (auto line : juce::StringArray::fromLines (markdown))
    {
        line = line.replace ("**", "").replace ("`", "").replace ("\r", "");
        while (line.startsWith ("#"))
            line = line.substring (1).trimStart();
        // [text](url) -> text
        for (int open = line.indexOf ("]("); open > 0; open = line.indexOf ("]("))
        {
            const auto start = line.substring (0, open).lastIndexOf ("["), end = line.indexOf (open, ")");
            if (start < 0 || end < 0)
                break;
            line = line.substring (0, start) + line.substring (start + 1, open) + line.substring (end + 1);
        }
        if (line.startsWith ("- "))
            line = juce::String::fromUTF8 ("\xe2\x80\xa2 ") + line.substring (2);
        lines.add (line);
    }
    return lines.joinIntoString ("\n").trim();
}

juce::String describe (const Updater::Part& part)
{
    const auto where = part.location.getParentDirectory().getFullPathName().replace (
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName(), "~");
    const auto name = part.kind == "App" ? juce::String ("The app") : part.kind == "AU" ? juce::String ("The AU plugin") : juce::String ("The VST3 plugin");
    return name + " in " + where;
}
} // namespace

UpdateCard::UpdateCard (UpdateCheck::Release r) : release (std::move (r))
{
    setLookAndFeel (&look());
    notes.setMultiLine (true);
    notes.setReadOnly (true);
    notes.setCaretVisible (false);
    notes.setScrollbarsShown (true);
    notes.setFont (juce::FontOptions (13.0f));
    notes.setText (plainNotes (release.notes), false);
    addAndMakeVisible (notes);

    installButton.getProperties().set ("primary", true);
    relaunchButton.getProperties().set ("primary", true);
    installButton.onClick = [this] { install(); };
    relaunchButton.onClick = [this] { relaunch(); };
    pageButton.onClick = [this] { release.page.launchInDefaultBrowser(); };
    laterButton.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &laterButton, &pageButton, &installButton, &relaunchButton })
        addAndMakeVisible (b);
    setSize (cardWidth, cardHeight);
    show (State::available);
}

UpdateCard::~UpdateCard()
{
    updater = nullptr; // waits for (or stops) the work in progress
    setLookAndFeel (nullptr);
}

void UpdateCard::show (State s, float p, const juce::String& m)
{
    state = s;
    progress = p;
    message = m;
    const bool app = juce::JUCEApplicationBase::isStandaloneApp();
    installButton.setVisible (s == State::available);
    pageButton.setVisible (s == State::available || s == State::failed);
    relaunchButton.setVisible (s == State::installed && app);
    laterButton.setVisible (s != State::installing);
    laterButton.setButtonText (s == State::available ? "Later" : s == State::installed && app ? "Later" : "Close");
    resized();
    repaint();
}

void UpdateCard::install()
{
    show (State::installing, 0.0f, juce::String::fromUTF8 ("Starting\xe2\x80\xa6"));
    updater = std::make_unique<Updater> (
        release,
        [safe = juce::Component::SafePointer<UpdateCard> (this)] (float p, const juce::String& status) {
            if (safe != nullptr && safe->state == State::installing)
                safe->show (State::installing, p, status);
        },
        [safe = juce::Component::SafePointer<UpdateCard> (this)] (bool ok, const juce::String& result) {
            if (safe != nullptr)
                safe->show (ok ? State::installed : State::failed, 1.0f, result);
        });
}

void UpdateCard::relaunch()
{
    // Open the (new) app a moment after this one has quit.
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::ChildProcess opener;
    if (opener.start (juce::StringArray { "/bin/sh", "-c", "sleep 1; /usr/bin/open \"" + app.getFullPathName() + "\"" }, 0))
        juce::JUCEApplicationBase::quit();
}

void UpdateCard::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds(), state == State::installed ? "Updated" : state == State::failed ? "Update didn't finish" : "Update available");
    g.setColour (line);
    g.fillRect (pad, 52, getWidth() - 2 * pad, 1);

    // The top: what's on offer, the progress, or how it went.
    auto top = topArea;
    g.setColour (CardStyle::text);
    g.setFont (juce::FontOptions (14.5f));
    const auto current = juce::String ("v") + JucePlugin_VersionString;
    if (state == State::available)
        g.drawText ("Peak Editor " + release.version + " is out (you have " + current + ").", top.removeFromTop (22), juce::Justification::centredLeft);
    else if (state == State::installing)
    {
        g.drawText ("Installing Peak Editor " + release.version + juce::String::fromUTF8 ("\xe2\x80\xa6"), top.removeFromTop (22), juce::Justification::centredLeft);
        const auto bar = top.removeFromTop (24).withTrimmedTop (8).toFloat();
        g.setColour (cell);
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (barFill);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (10.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress))), 5.0f);
        g.setColour (line);
        g.drawRoundedRectangle (bar, 5.0f, 1.0f);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (message, top.removeFromTop (22).withTrimmedTop (4), juce::Justification::centredLeft);
    }
    else
    {
        g.drawFittedText (message, top.removeFromTop (state == State::failed ? 40 : 22), juce::Justification::topLeft, 3);
        g.setColour (dim);
        g.setFont (juce::FontOptions (13.0f));
        const auto next = state == State::failed ? juce::String ("Anything that couldn't be updated was left as it was. The release page has the installer too.")
                          : juce::JUCEApplicationBase::isStandaloneApp()
                              ? juce::String ("Relaunch to start the new version. The plugins are updated too: reopen your DAW projects to use them.")
                              : juce::String ("Reopen your project (or restart the DAW) to use the new plugin. The app is updated too.");
        g.drawFittedText (next, top.withTrimmedTop (2), juce::Justification::topLeft, 2);
    }

    g.setColour (blue);
    g.setFont (headingFont (11.0f));
    g.drawText ("WHAT'S NEW IN " + release.version.toUpperCase(), notes.getBounds().translated (0, -22).withHeight (18), juce::Justification::centredLeft);
    if (state == State::available)
    {
        // What Install Update will replace.
        auto p = partsArea;
        g.drawText ("WILL BE UPDATED", p.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.5f));
        for (const auto& part : Updater::parts())
            g.drawText (juce::String::fromUTF8 ("\xe2\x80\xa2 ") + describe (part), p.removeFromTop (18), juce::Justification::centredLeft, true);
    }
}

void UpdateCard::resized()
{
    auto r = getLocalBounds().reduced (pad, 0);
    r.removeFromTop (52 + 18);
    auto buttons = r.removeFromBottom (58).withTrimmedTop (16).withTrimmedBottom (8);
    const auto main = buttons.removeFromRight (150); // Install Update, or Relaunch Now afterwards
    installButton.setBounds (main);
    relaunchButton.setBounds (main);
    buttons.removeFromRight (10);
    pageButton.setBounds (buttons.removeFromRight (140));
    laterButton.setBounds (buttons.removeFromLeft (100));
    topArea = r.removeFromTop (state == State::available ? 22 : 70);
    if (state == State::available)
        partsArea = r.removeFromBottom (20 + 18 * 3 + 6);
    r.removeFromTop (30);
    notes.setBounds (r.withTrimmedBottom (12));
}
