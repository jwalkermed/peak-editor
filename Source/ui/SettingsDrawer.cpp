#include "SettingsDrawer.h"

#include "ui/CardStyle.h"
#include "ui/DrawerStyle.h"

#include <cstring>

namespace
{
using namespace DrawerStyle;

constexpr int lineHeight = 30, pageGap = 14, labelWidth = 130, controlWidth = 200, columnWidth = 380;

// A value bar like the mod matrix depth: filled from the minimum (or the centre for signed values).
class ValueBar final : public juce::Slider
{
public:
    explicit ValueBar (const peak::SettingDef& d) : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox), def (d)
    {
        setRange (d.min, d.max, 1);
        setMouseDragSensitivity (250);
        setScrollWheelEnabled (false); // drag only: the wheel scrolls the page
        setDoubleClickReturnValue (true, d.centre >= 0 ? d.centre : (d.min + d.max) / 2);
    }

    bool known = true;

    void paint (juce::Graphics& g) override
    {
        // As the menus' value bars.
        auto b = getLocalBounds().toFloat().reduced (0.5f, 2.5f);
        g.setColour (CardStyle::cell);
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (isMouseOverOrDragging() ? CardStyle::dim : CardStyle::line);
        g.drawRoundedRectangle (b, 5.0f, 1.0f);
        if (! known)
        {
            g.setColour (CardStyle::dim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("not read yet", b.reduced (10.0f, 0.0f), juce::Justification::centredLeft, false);
            return;
        }
        const auto xFor = [&] (double v) { return b.getX() + 2.0f + (b.getWidth() - 4.0f) * (float) ((v - def.min) / (double) (def.max - def.min)); };
        const auto from = xFor (def.centre >= 0 ? def.centre : def.min), to = xFor (getValue());
        if (std::abs (to - from) > 1.0f)
        {
            g.setColour (CardStyle::barFill);
            g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, to), b.getY() + 2.0f, juce::jmax (from, to), b.getBottom() - 2.0f), 4.0f);
        }
        if (def.centre >= 0)
        {
            g.setColour (CardStyle::dim);
            g.fillRect (juce::Rectangle<float> (1.0f, b.getHeight() - 8.0f).withCentre ({ from, b.getCentreY() }));
        }
        g.setColour (CardStyle::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::String (peak::formatSetting (def, (int) getValue())), b.reduced (10.0f, 0.0f), juce::Justification::centredRight, false);
    }

private:
    const peak::SettingDef& def;
};
} // namespace

struct SettingsDrawer::Row
{
    const peak::SettingDef* def = nullptr;
    juce::Label label;
    std::unique_ptr<juce::ComboBox> choices;
    std::unique_ptr<ValueBar> bar;
};

SettingsDrawer::SettingsDrawer (PeakModel& m) : model (m)
{
    setLookAndFeel (&CardStyle::look());
    title.setFont (font (14.0f));
    title.setColour (juce::Label::textColourId, silkscreen);
    addAndMakeVisible (title);
    for (auto* l : { &status, &hint })
    {
        l->setFont (juce::FontOptions (13.0f));
        l->setColour (juce::Label::textColourId, silkscreen.withAlpha (0.65f));
        addAndMakeVisible (l);
    }
    hint.setText ("Changes go straight to the Peak. MidiChan and CC/NRPN are locked here: changing them could cut the editor off "
                  "from the Peak, so set them on the Peak itself. Tuning tables are edited on the Peak or with Novation Components.",
                  juce::dontSendNotification);
    hint.setJustificationType (juce::Justification::topLeft);

    readButton.setTooltip ("The Peak sends its settings from its Backup page");
    readButton.onClick = [this] {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("Reading the settings")
                                          .withMessage ("On the Peak: Settings > page right to Backup, Select = Settings, Send To = the port this "
                                                        "editor listens on, then Go. The values here update as it arrives.")
                                          .withButton ("OK", 0)
                                          .withAssociatedComponent (this),
                                      [] (int) {});
    };
    localOn.setTooltip ("Local control On: the Peak's panel controls play its sounds (CC 122)");
    localOff.setTooltip ("Local control Off: the panel only sends MIDI (CC 122)");
    localOn.onClick = [this] { model.setLocalControl (true); };
    localOff.onClick = [this] { model.setLocalControl (false); };
    localLabel.setFont (juce::FontOptions (14.0f));
    localLabel.setColour (juce::Label::textColourId, silkscreen);
    for (auto* b : { &readButton, &localOn, &localOff })
    {
        DrawerStyle::style (*b);
        addAndMakeVisible (b);
    }
    addAndMakeVisible (localLabel);

    const char* lastPage = "";
    for (const auto& s : peak::allSettings())
    {
        if (std::strcmp (s.page, lastPage) != 0)
        {
            lastPage = s.page;
            auto& t = pageTitles.emplace_back (std::make_unique<juce::Label> (juce::String(), juce::String (s.page).toUpperCase()));
            t->setFont (font (11.0f));
            t->setColour (juce::Label::textColourId, CardStyle::blue);
            addAndMakeVisible (*t);
        }

        auto row = std::make_unique<Row>();
        row->def = &s;
        row->label.setText (s.name, juce::dontSendNotification);
        row->label.setFont (juce::FontOptions (14.0f));
        row->label.setColour (juce::Label::textColourId, silkscreen.withAlpha (s.readOnly ? 0.5f : 1.0f));
        addAndMakeVisible (row->label);

        if (! s.names.empty())
        {
            row->choices = std::make_unique<juce::ComboBox>();
            for (size_t i = 0; i < s.names.size(); ++i)
                row->choices->addItem (s.names[i], (int) i + 1);
            row->choices->setEnabled (! s.readOnly);
            row->choices->onChange = [this, r = row.get()] {
                if (! updating && r->choices->getSelectedId() > 0)
                    model.setSetting (*r->def, r->def->min + r->choices->getSelectedId() - 1);
            };
            addAndMakeVisible (*row->choices);
        }
        else
        {
            row->bar = std::make_unique<ValueBar> (s);
            row->bar->onValueChange = [this, r = row.get()] {
                if (! updating)
                    model.setSetting (*r->def, (int) r->bar->getValue());
            };
            addAndMakeVisible (*row->bar);
        }
        rows.push_back (std::move (row));
    }

    model.changed.addChangeListener (this);
    refresh();
}

SettingsDrawer::~SettingsDrawer()
{
    setLookAndFeel (nullptr);
    model.changed.removeChangeListener (this);
}

void SettingsDrawer::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (pinstripe);
    g.fillRect (12, 34, getWidth() - 24, 1);
}

void SettingsDrawer::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (24);
    title.setBounds (top.removeFromLeft (110));
    readButton.setBounds (top.removeFromRight (150).reduced (2, 0));
    status.setBounds (top);
    r.removeFromTop (12);
    hint.setBounds (r.removeFromBottom (44));

    // Pages flow down columns, like the Peak's pages one after another.
    const int columns = juce::jmax (1, r.getWidth() / columnWidth);
    const int colW = r.getWidth() / columns;
    int col = 0, y = r.getY();
    size_t page = 0;
    const char* lastPage = "";
    const auto place = [&] (int height) {
        if (y + height > r.getBottom() && col < columns - 1)
        {
            ++col;
            y = r.getY();
        }
        const auto area = juce::Rectangle<int> (r.getX() + col * colW, y, colW - 16, height);
        y += height;
        return area;
    };

    for (auto& row : rows)
    {
        if (std::strcmp (row->def->page, lastPage) != 0)
        {
            lastPage = row->def->page;
            // A page title stays with its first setting: start a new column if both don't fit.
            if (y + pageGap + (lineHeight - 6) + lineHeight > r.getBottom() && col < columns - 1)
            {
                ++col;
                y = r.getY();
            }
            else if (page > 0)
                y += pageGap; // space between pages
            pageTitles[page++]->setBounds (place (lineHeight - 6));
        }
        auto line = place (lineHeight).reduced (0, 3);
        row->label.setBounds (line.removeFromLeft (labelWidth));
        auto control = line.removeFromLeft (controlWidth);
        if (row->choices)
            row->choices->setBounds (control);
        else
            row->bar->setBounds (control);

        // Local control sits with the MIDI Control page, after MidiChan.
        if (std::strcmp (row->def->id, "midi_chan") == 0)
        {
            auto local = place (lineHeight).reduced (0, 3);
            localLabel.setBounds (local.removeFromLeft (labelWidth));
            localOn.setBounds (local.removeFromLeft (controlWidth / 2 - 2));
            local.removeFromLeft (4);
            localOff.setBounds (local.removeFromLeft (controlWidth / 2 - 2));
        }
    }
}

void SettingsDrawer::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void SettingsDrawer::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    status.setText (model.hasSettings() ? model.getSettingsSource()
                                        : juce::String ("Not read yet: send the settings from the Peak's Backup page"),
                    juce::dontSendNotification);
    for (auto& row : rows)
    {
        const auto v = model.getSetting (*row->def);
        if (row->choices)
            row->choices->setSelectedId (v >= 0 ? v - row->def->min + 1 : 0, juce::dontSendNotification);
        else
        {
            row->bar->known = v >= 0;
            if (v >= 0)
                row->bar->setValue (v, juce::dontSendNotification);
            row->bar->repaint();
        }
    }
}
