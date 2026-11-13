// Renders the front panel to a PNG for visual checks against the reference photo.
// Usage: PanelSnapshot out.png [width] [patch.syx] [param_id=value]  (the last shows the screen just after an edit)
#include "peak/PeakModel.h"
#include "ui/FrontPanel.h"
#include "ui/LibraryDrawer.h"
#include "ui/ModAssign.h"
#include "peak/core/ParamFormat.h"
#include "ui/ModMatrixDrawer.h"
#include "ui/SettingsDrawer.h"
#include "ui/WavetableEditor.h"
#include "ui/UpdateCard.h"
#include "ui/MidiMonitorPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 2)
    {
        std::printf ("usage: PanelSnapshot out.png [width] [patch.syx]\n");
        return 1;
    }

    const auto width = argc > 2 ? juce::jmax (100, juce::String (argv[2]).getIntValue()) : 2191;
    juce::SharedResourcePointer<PeakModel> model;

    if (argc > 3)
    {
        juce::MemoryBlock data;
        juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]).loadFileAsData (data);
        const auto* bytes = static_cast<const uint8_t*> (data.getData());
        for (const auto& msg : peak::splitSysex (bytes, data.getSize()))
            if (const auto p = peak::Patch::fromSysex (msg))
                model->loadPatch (*p, "snapshot");
    }

    // "modassign": list each parameter's right-click modulation targets (a check of ModAssign's mapping).
    if (juce::String (argc > 2 ? argv[2] : "") == "modassign")
    {
        for (const auto& p : peak::allParams())
        {
            const auto d = ModAssign::destinationFor (p), f = ModAssign::fxDestinationFor (p);
            const auto s = ModAssign::sourcesFor (p);
            if (d >= 0 || f >= 0 || ! s.isEmpty())
                std::printf ("%-24s dest=%-18s fx=%-18s sources=%d\n", p.id, d >= 0 ? peak::modDestinationNames()[(size_t) d].c_str() : "-",
                             f >= 0 ? peak::fxDestinationNames()[(size_t) f].c_str() : "-", s.size());
        }
        return 0;
    }

    // "library", "settings", "modmatrix" or "menu:<button>" as the width renders that drawer instead of the panel.
    std::unique_ptr<juce::Component> view;
    if (juce::String (argc > 2 ? argv[2] : "") == "library")
    {
        auto drawer = std::make_unique<LibraryDrawer> (*model);
        drawer->setSize (900, 640);
        drawer->setAllExpanded (true);
        view = std::move (drawer);
    }
    else if (juce::String (argc > 2 ? argv[2] : "") == "libraryhelp")
    {
        auto drawer = std::make_unique<LibraryDrawer> (*model);
        drawer->setSize (900, 640);
        drawer->showHelp();
        view = std::move (drawer);
    }
    else if (juce::String (argc > 2 ? argv[2] : "") == "settings")
    {
        view = std::make_unique<SettingsDrawer> (*model);
        view->setSize (900, 640);
    }
    else if (juce::String (argc > 2 ? argv[2] : "") == "wavetables")
    {
        view = std::make_unique<WavetableEditor> (*model);
        view->setSize (1400, 860);
    }
    else if (juce::String (argc > 2 ? argv[2] : "").startsWith ("wavetablebrowser:"))
    {
        // wavetablebrowser:<folder>[|<file to select>]
        auto editor = std::make_unique<WavetableEditor> (*model);
        editor->setSize (1400, 860);
        const auto spec = juce::String (argv[2]).fromFirstOccurrenceOf (":", false, false);
        editor->browse (juce::File (spec.upToFirstOccurrenceOf ("|", false, false)), juce::File (spec.fromFirstOccurrenceOf ("|", false, false)));
        view = std::move (editor);
    }
    else if (juce::String (argc > 2 ? argv[2] : "").startsWith ("wavetablesample:") || juce::String (argc > 2 ? argv[2] : "").startsWith ("wavetablechoice:"))
    {
        // wavetablesample:<folder>|<file>|<seconds>, or wavetablechoice:<folder>|<file>
        auto editor = std::make_unique<WavetableEditor> (*model);
        editor->setSize (1400, 860);
        const auto parts = juce::StringArray::fromTokens (juce::String (argv[2]).fromFirstOccurrenceOf (":", false, false), "|", "");
        editor->browse (juce::File (parts[0]), juce::File (parts[1]));
        if (juce::String (argv[2]).startsWith ("wavetablesample:"))
            editor->openSample (juce::File (parts[1]), parts[2].getDoubleValue());
        else
            editor->askHowToLoad (juce::File (parts[1]));
        view = std::move (editor);
    }
    else if (juce::String (argc > 2 ? argv[2] : "").startsWith ("wavetablehome:"))
    {
        // wavetablehome:<folder>|<folder>|...: the file browser's Home list
        auto editor = std::make_unique<WavetableEditor> (*model);
        editor->setSize (1400, 860);
        juce::Array<juce::File> folders;
        for (const auto& f : juce::StringArray::fromTokens (juce::String (argv[2]).fromFirstOccurrenceOf (":", false, false), "|", ""))
            folders.add (juce::File (f));
        editor->browseHome (folders);
        view = std::move (editor);
    }
    else if (juce::String (argc > 2 ? argv[2] : "").startsWith ("updatertest:"))
    {
        // updatertest:<version>|<folder with that release's three zips>: installs them as Install Update would.
        const auto parts = juce::StringArray::fromTokens (juce::String (argv[2]).fromFirstOccurrenceOf (":", false, false), "|", "");
        UpdateCheck::Release release;
        release.version = parts[0];
        for (const auto& zip : juce::File (parts[1]).findChildFiles (juce::File::findFiles, false, "*.zip"))
            release.downloads.set (zip.getFileName(), juce::URL (zip).toString (false));
        bool finished = false;
        Updater updater (release, [] (float p, const juce::String& s) { printf ("%3d%% %s\n", (int) (p * 100), s.toRawUTF8()); },
                         [&finished] (bool ok, const juce::String& m) { printf ("%s: %s\n", ok ? "OK" : "FAILED", m.toRawUTF8()); finished = true; });
        for (int i = 0; i < 600 && ! finished; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        return finished ? 0 : 1;
    }
    else if (juce::String (argc > 2 ? argv[2] : "").startsWith ("update:"))
    {
        // update:available|installing|installed|failed, for an example release
        UpdateCheck::Release release;
        release.version = "v1.4.0";
        release.notes = "An editor and librarian for the Novation Peak.\n\n## New in 1.4.0\n- **One-click updates:** Install Update downloads "
                        "the new version and replaces the app and plugins.\n- A [README](https://example.com) section on updating.\n\n"
                        "## Installing\nDownload the zips and run the installer.";
        auto card = std::make_unique<UpdateCard> (release);
        const auto state = juce::String (argv[2]).fromFirstOccurrenceOf (":", false, false);
        if (state == "installing")
            card->show (UpdateCard::State::installing, 0.42f, juce::String::fromUTF8 ("Downloading Peak-Editor-1.4.0-macOS-AU.zip\xe2\x80\xa6"));
        else if (state == "installed")
            card->show (UpdateCard::State::installed, 1.0f, "Updated to v1.4.0.");
        else if (state == "failed")
            card->show (UpdateCard::State::failed, 1.0f, "Couldn't install /Applications/Peak Editor.app (no permission?).\nUpdated: AU, VST3.");
        view = std::move (card);
    }
    else if (juce::String (argc > 2 ? argv[2] : "") == "wavetablehelp")
    {
        auto editor = std::make_unique<WavetableEditor> (*model);
        editor->setSize (1400, 860);
        editor->showHelp();
        view = std::move (editor);
    }
    else if (juce::String (argc > 2 ? argv[2] : "") == "midi")
    {
        view = std::make_unique<MidiMonitorPanel> (model->getConnection());
        view->setSize (1200, 500);
    }
    else if (juce::String (argc > 2 ? argv[2] : "").startsWith ("wave:"))
    {
        // wave:<oscillator>[:<WaveMore value whose notes card to show>]
        const auto spec = juce::StringArray::fromTokens (juce::String (argv[2]).fromFirstOccurrenceOf ("wave:", false, false), ":", "");
        view = FrontPanel::createWavePicker (*model, spec[0].getIntValue(), 2, spec.size() > 1 ? spec[1].getIntValue() : -1);
    }
    else if (juce::String (argc > 2 ? argv[2] : "").startsWith ("menu:"))
    {
        view = FrontPanel::createMenuPage (*model, juce::String (argv[2]).fromFirstOccurrenceOf ("menu:", false, false));
    }
    else if (juce::String (argc > 2 ? argv[2] : "") == "modmatrix")
    {
        view = std::make_unique<ModMatrixDrawer> (*model);
        view->setSize (900, 760);
    }
    else
    {
        auto panel = std::make_unique<FrontPanel> (*model);
        const auto d = FrontPanel::designBounds();
        panel->setSize (width, juce::roundToInt ((float) width * d.getHeight() / d.getWidth()));
        if (argc > 4)
            if (const auto* p = peak::findParam (juce::String (argv[4]).upToFirstOccurrenceOf ("=", false, false).toStdString()))
            {
                model->set (*p, juce::String (argv[4]).fromFirstOccurrenceOf ("=", false, false).getIntValue());
                model->changed.dispatchPendingMessages();
            }
        view = std::move (panel);
    }

    // Menu pages are drawn on the call-out's dark background.
    juce::Image image (juce::Image::ARGB, view->getWidth(), view->getHeight(), true);
    {
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff1e1f22));
        view->paintEntireComponent (g, true);
    }
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return 0;
}
