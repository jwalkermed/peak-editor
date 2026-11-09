// Renders the front panel to a PNG for visual checks against the reference photo.
// Usage: PanelSnapshot out.png [width] [patch.syx]
#include "peak/PeakModel.h"
#include "ui/FrontPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 2)
    {
        std::printf ("usage: PanelSnapshot out.png [width] [patch.syx]\n");
        return 1;
    }

    const auto width = argc > 2 ? juce::String (argv[2]).getIntValue() : 2191;
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

    FrontPanel panel (*model);
    const auto d = FrontPanel::designBounds();
    panel.setSize (width, juce::roundToInt ((float) width * d.getHeight() / d.getWidth()));

    const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return 0;
}
