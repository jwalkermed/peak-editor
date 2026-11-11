#include "ParamRows.h"

#include "peak/core/ParamFormat.h"
#include "ui/CardStyle.h"
#include "ui/ModAssign.h"
#include "ui/DrawerStyle.h"

#include <cstring>

namespace
{
constexpr int rowHeight = 30, headerHeight = 40, labelWidth = 150;
}

ParamRows::ParamRows (PeakModel& m, const std::vector<const peak::ParamDef*>& params) : model (m)
{
    setLookAndFeel (&CardStyle::look());
    // Double-clicking a row's name puts its value back as saved in the patch (also for drop-downs).
    resetOnDoubleClick.onDoubleClick = [this] (juce::Component* c) {
        for (auto& r : rows)
            if (r.label.get() == c)
                model.set (*r.def, model.getStored (*r.def));
    };
    // Right-clicking a row's name or value: the modulation menu (as on the panel).
    resetOnDoubleClick.onRightClick = [this] (juce::Component* c) {
        for (auto& r : rows)
            if (r.label.get() == c || &r.control() == c || r.control().isParentOf (c))
                ModAssign::showMenu (model, *r.def);
    };
    const char* lastSection = "";
    for (const auto* pp : params)
    {
        const auto& p = *pp;
        if (std::strcmp (p.section, lastSection) != 0)
        {
            lastSection = p.section;
            auto& h = headers.emplace_back (std::make_unique<juce::Label> (juce::String(), juce::String (p.section).toUpperCase()));
            CardStyle::styleHeading (*h); // small blue capitals, with a line drawn under (paint)
            addAndMakeVisible (*h);
            items.push_back ({ h.get(), nullptr });
        }

        Row row;
        row.def = &p;
        row.label = std::make_unique<juce::Label> (juce::String(), juce::String (p.name) + (p.isStored() ? "" : "  (not saved)"));
        row.label->setColour (juce::Label::textColourId, CardStyle::text);
        row.label->setFont (juce::FontOptions (13.0f));
        row.label->setBorderSize ({ 0, 0, 0, 0 });
        const auto tooltip = juce::String (p.kind == peak::MidiKind::nrpn
                                               ? "NRPN " + juce::String (p.number >> 7) + ":" + juce::String (p.number & 0x7F)
                                               : "CC " + juce::String (p.number) + (p.kind == peak::MidiKind::ccPair ? "/" + juce::String (p.number + 32) : ""))
                             + (p.isStored() ? juce::String::fromUTF8 ("  ·  dump byte 0x") + juce::String::toHexString (p.offset).toUpperCase() : "");
        if (const auto names = peak::valueNames (p); ! names.empty())
        {
            row.choices = std::make_unique<juce::ComboBox>();
            for (size_t i = 0; i < names.size(); ++i)
                row.choices->addItem (juce::String::fromUTF8 (names[i].c_str()), (int) i + 1);
            row.choices->setTooltip (tooltip);
            row.choices->addMouseListener (&resetOnDoubleClick, true);
            row.choices->onChange = [this, def = row.def, c = row.choices.get()] {
                if (! updating && c->getSelectedId() > 0)
                    model.set (*def, def->min + c->getSelectedId() - 1);
            };
            row.label->addMouseListener (&resetOnDoubleClick, false);
            addAndMakeVisible (*row.label);
            addAndMakeVisible (*row.choices);
            rows.push_back (std::move (row));
            items.push_back ({ nullptr, &rows.back() });
            continue;
        }

        auto slider = std::make_unique<DrawerStyle::ResetSlider> (juce::Slider::LinearBar, juce::Slider::TextBoxRight);
        slider->resetValue = [this, def = row.def] { return model.getStored (*def); }; // double-click: as saved in the patch
        row.slider = std::move (slider);
        row.slider->setRange (p.min, p.max, 1);
        row.slider->textFromValueFunction = [def = row.def] (double v) { return juce::String (peak::formatValue (*def, (int) v)); };
        row.slider->setTextBoxIsEditable (false); // double-click resets instead of typing a value
        row.slider->setScrollWheelEnabled (false); // drag only: the wheel scrolls the page instead
        if (const auto centre = peak::defaultValue (p); centre != p.min) // shown as -/+: the bar fills from the centre
            row.slider->getProperties().set ("centre", centre);
        row.slider->setTooltip (tooltip);
        row.slider->addMouseListener (&resetOnDoubleClick, true);
        row.slider->onValueChange = [this, def = row.def, s = row.slider.get()] {
            if (! updating)
                model.set (*def, (int) s->getValue());
        };
        row.label->addMouseListener (&resetOnDoubleClick, false);
        addAndMakeVisible (*row.label);
        addAndMakeVisible (*row.slider);
        rows.push_back (std::move (row));
        items.push_back ({ nullptr, &rows.back() });
    }
    sendLookAndFeelChange(); // the sliders were made before they were children: rebuild their value boxes
}

ParamRows::~ParamRows()
{
    setLookAndFeel (nullptr);
}

void ParamRows::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    for (auto& r : rows)
    {
        if (r.slider)
            r.slider->setValue (model.get (*r.def), juce::dontSendNotification);
        else
            r.choices->setSelectedId (model.get (*r.def) - r.def->min + 1, juce::dontSendNotification);
    }
}

void ParamRows::paint (juce::Graphics& g)
{
    // A line under each section heading, as in the wavetable picker.
    g.setColour (CardStyle::line);
    for (const auto& h : headers)
        g.fillRect (h->getX(), h->getBottom() + 2, h->getWidth(), 1);
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
            it.header->setBounds (r.withTrimmedTop (12).withHeight (20));
        else
        {
            auto rr = r.reduced (0, 3);
            it.row->label->setBounds (rr.removeFromLeft (labelWidth));
            it.row->control().setBounds (rr);
        }
        y += h;
        maxY = juce::jmax (maxY, y);
    }
    setSize (width, maxY + 8);
}
