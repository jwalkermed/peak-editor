#include "ParameterListPanel.h"

#include "ui/MidiMonitorPanel.h"

#include <cstring>
#include <deque>

namespace
{
constexpr int rowHeight = 24, headerHeight = 30, columnWidth = 380;
}

struct ParameterListPanel::Row
{
    const peak::ParamDef* def = nullptr;
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<juce::Slider> slider;
};

class ParameterListPanel::Content final : public juce::Component
{
public:
    Content (PeakModel& m) : model (m)
    {
        const char* lastSection = "";
        for (const auto& p : peak::allParams())
        {
            if (std::strcmp (p.section, lastSection) != 0)
            {
                lastSection = p.section;
                auto& h = headers.emplace_back (std::make_unique<juce::Label> (juce::String(), juce::String (p.section).toUpperCase()));
                h->setColour (juce::Label::textColourId, juce::Colours::white);
                h->setFont (juce::FontOptions (14.0f, juce::Font::bold));
                addAndMakeVisible (*h);
                items.push_back ({ h.get(), nullptr });
            }

            Row row;
            row.def = &p;
            row.label = std::make_unique<juce::Label> (juce::String(), juce::String (p.name) + (p.isStored() ? "" : "  (not saved)"));
            row.label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
            row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxRight);
            row.slider->setRange (p.min, p.max, 1);
            row.slider->setColour (juce::Slider::trackColourId, MidiMonitorPanel::accentColour.withAlpha (0.55f));
            row.slider->setTooltip (juce::String (p.kind == peak::MidiKind::nrpn
                                                      ? "NRPN " + juce::String (p.number >> 7) + ":" + juce::String (p.number & 0x7F)
                                                      : "CC " + juce::String (p.number) + (p.kind == peak::MidiKind::ccPair ? "/" + juce::String (p.number + 32) : ""))
                                    + (p.isStored() ? "  ·  dump byte 0x" + juce::String::toHexString (p.offset).toUpperCase() : ""));
            row.slider->onValueChange = [this, def = row.def, s = row.slider.get()] {
                if (! updating)
                    model.set (*def, (int) s->getValue());
            };
            addAndMakeVisible (*row.label);
            addAndMakeVisible (*row.slider);
            rows.push_back (std::move (row));
            items.push_back ({ nullptr, &rows.back() });
        }
    }

    void refresh()
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        for (auto& r : rows)
            r.slider->setValue (model.get (*r.def), juce::dontSendNotification);
    }

    // Lays out sections top-to-bottom, flowing into as many columns as fit.
    void layout (int width)
    {
        const int columns = juce::jmax (1, width / columnWidth);
        const int colW = width / columns;
        int total = 0;
        for (auto& it : items)
            total += it.header ? headerHeight : rowHeight;
        const int perColumn = total / columns + headerHeight;

        int col = 0, y = 0, maxY = 0;
        for (auto& it : items)
        {
            const int h = it.header ? headerHeight : rowHeight;
            if (it.header && y + h > perColumn && col < columns - 1)
            {
                ++col;
                y = 0;
            }
            const juce::Rectangle<int> r (col * colW + 8, y, colW - 16, h);
            if (it.header)
                it.header->setBounds (r.withTrimmedTop (8));
            else
            {
                auto rr = r.reduced (0, 2);
                it.row->label->setBounds (rr.removeFromLeft (150));
                it.row->slider->setBounds (rr);
            }
            y += h;
            maxY = juce::jmax (maxY, y);
        }
        setSize (width, maxY + 8);
    }

private:
    struct Item
    {
        juce::Label* header;
        Row* row;
    };

    PeakModel& model;
    std::vector<std::unique_ptr<juce::Label>> headers;
    std::deque<Row> rows; // deque: stable addresses for items
    std::vector<Item> items;
    bool updating = false;
};

ParameterListPanel::ParameterListPanel (PeakModel& m) : model (m), content (std::make_unique<Content> (m))
{
    patchLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    patchLabel.setColour (juce::Label::textColourId, MidiMonitorPanel::ledColour);
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));

    sendButton.setTooltip ("Loads this patch into the Peak's edit buffer via CC/NRPN. No memory slot is written.");
    sendButton.onClick = [this] {
        if (! model.sendPatchToPeak())
            hintLabel.setText ("No MIDI output — choose the Peak's port on the MIDI Monitor tab", juce::dontSendNotification);
    };
    importButton.onClick = [this] { importSyx(); };
    exportButton.onClick = [this] { exportSyx(); };

    for (auto* c : std::initializer_list<juce::Component*> { &patchLabel, &hintLabel, &sendButton, &importButton, &exportButton, &viewport })
        addAndMakeVisible (c);

    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);

    model.changed.addChangeListener (this);
    refresh();
}

ParameterListPanel::~ParameterListPanel()
{
    model.changed.removeChangeListener (this);
}

void ParameterListPanel::paint (juce::Graphics& g)
{
    g.fillAll (MidiMonitorPanel::panelColour);
}

void ParameterListPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (28);
    exportButton.setBounds (top.removeFromRight (120).reduced (4, 0));
    importButton.setBounds (top.removeFromRight (120).reduced (4, 0));
    sendButton.setBounds (top.removeFromRight (160).reduced (4, 0));
    patchLabel.setBounds (top.removeFromLeft (220));
    hintLabel.setBounds (top);

    area.removeFromTop (8);
    viewport.setBounds (area);
    content->layout (area.getWidth() - viewport.getScrollBarThickness());
}

void ParameterListPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void ParameterListPanel::refresh()
{
    patchLabel.setText (model.hasPatch() ? juce::String (model.getPatch().name()) : "(no patch)", juce::dontSendNotification);
    hintLabel.setText (model.hasPatch() ? model.getSource()
                                        : "To load the Peak's current sound: Settings → Backup page, Select = Current, press Go",
                       juce::dontSendNotification);
    content->refresh();
}

void ParameterListPanel::importSyx()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a single-patch .syx", juce::File(), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::MemoryBlock data;
        file.loadFileAsData (data);
        const auto* bytes = static_cast<const uint8_t*> (data.getData());

        for (const auto& msg : peak::splitSysex (bytes, data.getSize()))
            if (const auto p = peak::Patch::fromSysex (msg))
            {
                model.loadPatch (*p, "Imported from " + file.getFileName() + " (not sent to Peak)");
                return;
            }

        hintLabel.setText (file.getFileName() + " contains no 527-byte Peak patch dump", juce::dontSendNotification);
    });
}

void ParameterListPanel::exportSyx()
{
    const auto name = juce::File::createLegalFileName (model.getPatch().name().empty() ? "patch" : juce::String (model.getPatch().name()).trim());
    chooser = std::make_unique<juce::FileChooser> ("Export patch as .syx",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".syx"), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              const auto bytes = model.getPatch().toSysex();
                              if (! file.replaceWithData (bytes.data(), bytes.size()))
                                  hintLabel.setText ("Could not write " + file.getFullPathName(), juce::dontSendNotification);
                          });
}
