#include "ParamRows.h"

#include "peak/core/ParamFormat.h"
#include "ui/MidiMonitorPanel.h"

#include <cstring>

namespace
{
constexpr int rowHeight = 24, headerHeight = 30;
}

ParamRows::ParamRows (PeakModel& m, const std::vector<const peak::ParamDef*>& params) : model (m)
{
    const char* lastSection = "";
    for (const auto* pp : params)
    {
        const auto& p = *pp;
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
        row.slider->textFromValueFunction = [def = row.def] (double v) { return juce::String (peak::formatValue (*def, (int) v)); };
        row.slider->setDoubleClickReturnValue (true, peak::defaultValue (p));
        row.slider->setColour (juce::Slider::trackColourId, MidiMonitorPanel::accentColour.withAlpha (0.55f));
        row.slider->setTooltip (juce::String (p.kind == peak::MidiKind::nrpn
                                                  ? "NRPN " + juce::String (p.number >> 7) + ":" + juce::String (p.number & 0x7F)
                                                  : "CC " + juce::String (p.number) + (p.kind == peak::MidiKind::ccPair ? "/" + juce::String (p.number + 32) : ""))
                                + (p.isStored() ? juce::String::fromUTF8 ("  ·  dump byte 0x") + juce::String::toHexString (p.offset).toUpperCase() : ""));
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

void ParamRows::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    for (auto& r : rows)
        r.slider->setValue (model.get (*r.def), juce::dontSendNotification);
}

// Lays out sections top-to-bottom, flowing into as many columns as fit.
void ParamRows::layout (int width, int columnWidth)
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
