// Renders the front panel to a PNG for visual checks against the reference photo.
// Usage: PanelSnapshot out.png [width] [patch.syx] [param_id=value]  (the last shows the screen just after an edit)
#include "peak/PeakModel.h"
#include "ui/FrontPanel.h"
#include "ui/LibraryDrawer.h"
#include "ui/ModMatrixDrawer.h"
#include "ui/SettingsDrawer.h"

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

    // "library", "settings", "modmatrix" or "menu:<button>" as the width renders that drawer instead of the panel.
    std::unique_ptr<juce::Component> view;
    if (juce::String (argc > 2 ? argv[2] : "") == "library")
    {
        auto drawer = std::make_unique<LibraryDrawer> (*model);
        drawer->setSize (900, 640);
        drawer->setAllExpanded (true);
        view = std::move (drawer);
    }
    else if (juce::String (argc > 2 ? argv[2] : "") == "settings")
    {
        view = std::make_unique<SettingsDrawer> (*model);
        view->setSize (900, 640);
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
