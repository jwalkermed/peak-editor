#include "FrontPanel.h"

#include "peak/core/ParamFormat.h"
#include "peak/core/WavetableInfo.h"
#include "ui/ParamRows.h"
#include "ui/CardStyle.h"
#include "ui/DisplayGraphs.h"
#include "ui/DrawerStyle.h"
#include "ui/ModAssign.h"
#include "ui/SlotWriter.h"

#include <cstring>
#include <string_view>

namespace
{
namespace colours
{
const juce::Colour panel { 0xff26272a }, panelLight { 0xff303134 }, rule { 0xff62a6dc }, text { 0xffeeeeee };
const juce::Colour ledOff { 0xff2b2b2c }, ledOn { 0xffffa63c }, lit { 0xfffff0b8 }, litEdge { 0xffffb347 };
const juce::Colour wood { 0xff6b4429 }, woodDark { 0xff4a2d1a }, oled { 0xffd9ecff };
} // namespace colours

constexpr float knobDotRadius = 48.2f, knobSkirt = 38.0f, knobCap = 22.0f, bigScale = 1.5f;
constexpr float sweep = juce::MathConstants<float>::pi * 5.0f / 6.0f; // +-150 degrees
constexpr float woodWidth = 44.0f;

float normalised (const peak::ParamDef& p, int v)
{
    return p.max > p.min ? (float) (v - p.min) / (float) (p.max - p.min) : 0.0f;
}

juce::String utf8 (const char* s)
{
    return juce::String::fromUTF8 (s);
}

// Small waveform icons for LED labels written as "~name".
void drawWaveIcon (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> r)
{
    juce::Path p;
    const auto x0 = r.getX(), x1 = r.getRight(), y0 = r.getY(), y1 = r.getBottom(), ym = r.getCentreY(), xm = r.getCentreX();
    if (name == "sine")
    {
        p.startNewSubPath (x0, ym);
        for (int i = 1; i <= 16; ++i)
            p.lineTo (x0 + (x1 - x0) * (float) i / 16.0f, ym - std::sin ((float) i / 16.0f * juce::MathConstants<float>::twoPi) * (ym - y0));
    }
    else if (name == "tri")
    {
        p.startNewSubPath (x0, y1);
        p.lineTo (xm, y0);
        p.lineTo (x1, y1);
    }
    else if (name == "saw")
    {
        p.startNewSubPath (x0, y1);
        p.lineTo (x1, y0);
        p.lineTo (x1, y1);
    }
    else // square / pulse
    {
        const auto w = name == "pulse" ? 0.3f : 0.5f;
        p.startNewSubPath (x0, y1);
        p.lineTo (x0, y0);
        p.lineTo (x0 + (x1 - x0) * w, y0);
        p.lineTo (x0 + (x1 - x0) * w, y1);
        p.lineTo (x1, y1);
        p.lineTo (x1, y0);
    }
    g.strokePath (p, juce::PathStrokeType (1.8f));
}

juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// Which parameter sections each menu button / section arrow opens.
std::vector<const char*> menuSections (const juce::String& menu)
{
    if (menu == "Osc") return { "Osc Common", "Oscillator 1", "Oscillator 2", "Oscillator 3" };
    if (menu.startsWith ("Oscillator")) return { "Oscillator 1", "Oscillator 2", "Oscillator 3", "Osc Common" };
    if (menu == "Env") return { "Amp Envelope", "Mod Envelope 1", "Mod Envelope 2" };
    if (menu == "Animate") return { "Animate" };
    if (menu == "LFO") return { "LFO 1", "LFO 2", "LFO 3", "LFO 4" };
    if (menu == "Arp/Clock") return { "Arp" }; // the Clock section is drawn above it (ClockRows)
    if (menu == "Mod") return { "Mod Matrix" };
    if (menu == "Voice") return { "Voice", "Glide", "Mixer" };
    if (menu == "FX") return { "Effects", "Chorus", "Delay", "Reverb" }; // the FX matrix is on the Mod Matrix tab
    if (menu == "Filter") return { "Filter" };
    if (menu == "Patch") return { "Patch" };
    return {};
}


// The Arp/Clock menu's Clock section: Clock Rate (in the patch, no MIDI: sent as a patch dump),
// Source (a global setting, NRPN 64:6) and the status line the Peak shows (INT 97.00bpm).
class ClockRows final : public juce::Component,
                        private juce::Timer
{
public:
    explicit ClockRows (PeakModel& m) : model (m)
    {
        setLookAndFeel (&CardStyle::look());
        CardStyle::styleHeading (header);
        for (auto* l : { &rateLabel, &sourceLabel, &statusLabel })
        {
            l->setColour (juce::Label::textColourId, CardStyle::text);
            l->setFont (juce::FontOptions (13.0f));
            l->setBorderSize ({ 0, 0, 0, 0 });
        }
        status.setColour (juce::Label::textColourId, CardStyle::text);
        status.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
        status.setTooltip ("As the Peak shows it while no external clock is coming in");

        const auto& p = *peak::findParam ("clock_rate");
        rate.setRange (p.min, p.max, 1);
        rate.textFromValueFunction = [&p] (double v) { return juce::String (peak::formatValue (p, (int) v)); };
        rate.resetValue = [this, &p] { return model.getStored (p); };
        rate.setTextBoxIsEditable (false);
        rate.setScrollWheelEnabled (false); // drag only, as in the other menu rows
        rate.setTooltip ("Stored in the patch; the Peak has no MIDI message for it, so changes go as the whole patch");
        rate.onValueChange = [this, &p] { if (! updating) model.set (p, (int) rate.getValue()); };

        const auto& s = *peak::findSetting ("clock_source");
        for (size_t i = 0; i < s.names.size(); ++i)
            source.addItem (s.names[i], (int) i + 1);
        source.setTooltip ("Global setting: the Peak's clock source (NRPN 64:6)");
        source.onChange = [this, &s] { if (! updating && source.getSelectedId() > 0) model.setSetting (s, source.getSelectedId() - 1); };

        for (auto* c : std::initializer_list<juce::Component*> { &header, &rateLabel, &rate, &sourceLabel, &source, &statusLabel, &status })
            addAndMakeVisible (c);
        setSize (364, 40 + 3 * 30);
        refresh();
        startTimerHz (4); // the DAW's clock can start, stop or change tempo at any time
        sendLookAndFeelChange(); // rebuild the rate bar's value box in the card style
    }

    ~ClockRows() override { setLookAndFeel (nullptr); }

    void refresh()
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        const auto& p = *peak::findParam ("clock_rate");
        const auto bpm = model.get (p);
        if (! rate.isMouseButtonDown()) // not while it's being dragged
            rate.setValue (bpm, juce::dontSendNotification);
        const auto src = model.getSetting (*peak::findSetting ("clock_source"));
        source.setSelectedId (src >= 0 ? src + 1 : 0, juce::dontSendNotification);
        // As the Peak shows it. With the plugin sending the DAW's clock, the Peak follows it unless
        // Source is Internal; when the DAW stops, Auto goes back to Clock Rate and the others
        // "flywheel" at the last tempo (FLY). Clock from elsewhere can't be seen from here.
        const auto& c = model.getConnection();
        const juce::String port = src == 4 ? "USB" : "MIDI";
        juce::String text = (src == 3 || src == 4 ? port : juce::String ("INT")) + "  " + juce::String (bpm) + ".00bpm";
        if (src != 1 && c.dawClockRunning)
            text = port + "  " + juce::String (c.dawClockBpm.load(), 2) + "bpm";
        else if (src != 1 && src != 0 && c.dawClockSent)
            text = "FLY  " + juce::String (c.dawClockBpm.load(), 2) + "bpm";
        status.setText (text, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (8).withTrimmedRight (8);
        header.setBounds (r.removeFromTop (40).withTrimmedTop (12).withHeight (20));
        for (auto [label, control] : { std::pair<juce::Component*, juce::Component*> { &rateLabel, &rate }, { &sourceLabel, &source }, { &statusLabel, &status } })
        {
            auto row = r.removeFromTop (30).reduced (0, 3);
            label->setBounds (row.removeFromLeft (150));
            control->setBounds (row);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (CardStyle::line);
        g.fillRect (header.getX(), header.getBottom() + 2, header.getWidth(), 1);
    }

private:
    void timerCallback() override { refresh(); }

    PeakModel& model;
    juce::Label header { {}, "CLOCK" }, rateLabel { {}, "Clock Rate" }, sourceLabel { {}, "Source" }, statusLabel { {}, "Status" }, status;
    DrawerStyle::ResetSlider rate { juce::Slider::LinearBar, juce::Slider::TextBoxRight };
    juce::ComboBox source;
    bool updating = false;
};

// A card over the panel with a Close button (see CardStyle::Overlay).
struct ClosableCard : juce::Component
{
    std::function<void()> onClose;  // Close, or a click outside
    std::function<void()> onEscape; // Esc (default: close)
};

// A white button's menu page, on a card: its sections' parameters as value cells and drop-downs.
class MenuPage final : public ClosableCard,
                       private juce::ChangeListener
{
public:
    MenuPage (PeakModel& m, const juce::String& titleText, std::vector<const peak::ParamDef*> params, bool withClock = false)
        : model (m), title (titleText.toUpperCase()), rows (m, params)
    {
        setLookAndFeel (&CardStyle::look());
        if (withClock)
        {
            clock = std::make_unique<ClockRows> (m);
            addAndMakeVisible (*clock);
        }
        viewport.setViewedComponent (&rows, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
        close.getProperties().set ("primary", true);
        close.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (close);
        rows.refresh();
        rows.layout (contentWidth, contentWidth / 2);
        setSize (contentWidth + 2 * pad, juce::jmin (680, top + (clock ? clock->getHeight() : 0) + rows.getHeight() + footer));
        model.changed.addChangeListener (this);
    }

    ~MenuPage() override
    {
        model.changed.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        CardStyle::paintCard (g, getLocalBounds(), title, "Double-click a name or value: back to the saved patch");
        g.setColour (CardStyle::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Drag a value to change it; changes go straight to the Peak.", getLocalBounds().reduced (pad, 0).removeFromBottom (footer),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad - 8, 0);
        r.removeFromTop (top - 12);
        auto bottom = r.removeFromBottom (footer);
        close.setBounds (bottom.withTrimmedRight (8).removeFromRight (100).withSizeKeepingCentre (100, 30));
        if (clock)
            clock->setBounds (r.removeFromTop (clock->getHeight()).withWidth (contentWidth / 2));
        viewport.setBounds (r);
    }

private:
    static constexpr int pad = 18, top = 52, footer = 56, contentWidth = 744;

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        rows.refresh();
        if (clock)
            clock->refresh();
    }

    PeakModel& model;
    juce::String title;
    std::unique_ptr<ClockRows> clock;
    ParamRows rows;
    juce::Viewport viewport;
    juce::TextButton close { "Close" };
};

// The wavetables for an oscillator's More setting: click one to hear it (it's sent to the Peak at
// once); Close keeps it, Cancel or Esc goes back to the one it started with. The user wavetables'
// names are read from the Peak (Refresh reads them again).
class WavePicker final : public ClosableCard,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    WavePicker (PeakModel& m, int oscillator, int waveBefore)
        : model (m), osc (oscillator), more (*peak::findParam ("osc" + std::to_string (oscillator) + "_wave_more")),
          wave (*peak::findParam ("osc" + std::to_string (oscillator) + "_wave")),
          shape (*peak::findParam ("osc" + std::to_string (oscillator) + "_manual_shape")), startWave (waveBefore), startMore (m.get (more)), startShape (m.get (shape))
    {
        setLookAndFeel (&CardStyle::look());

        // Wave Table Position: the oscillator's manual Shape Amount, which moves through the wavetable.
        position.setRange (shape.min, shape.max, 1);
        position.textFromValueFunction = [this] (double v) { return juce::String (peak::formatValue (shape, (int) v)); };
        position.resetValue = [this] { return model.getStored (shape); };
        position.getProperties().set ("centre", peak::defaultValue (shape));
        position.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        position.setTextBoxIsEditable (false);
        position.setScrollWheelEnabled (false);
        position.setTooltip ("The oscillator's Shape Amount (Manual): moves through the wavetable's waves");
        position.setValue (model.get (shape), juce::dontSendNotification);
        position.onValueChange = [this] { if (! updating) model.set (shape, (int) position.getValue()); };
        addAndMakeVisible (position);
        positionLabel.setFont (juce::FontOptions (13.0f));
        positionLabel.setColour (juce::Label::textColourId, CardStyle::text);
        positionLabel.setBorderSize ({ 0, 0, 0, 0 });
        addAndMakeVisible (positionLabel);
        refresh.onClick = [this] { model.requestUserWavetableNames(); };
        cancel.onClick = [this] { goBack(); };
        close.onClick = [this] { if (onClose) onClose(); };
        close.getProperties().set ("primary", true);
        refresh.setTooltip ("Read the user wavetables' names from the Peak again (after loading new ones with Components)");
        for (auto* b : { &refresh, &cancel, &close })
            addAndMakeVisible (b);
        onEscape = [this] { goBack(); };
        model.changed.addChangeListener (this);
        sendLookAndFeelChange(); // the position bar's value box in the card style
        setSize (2 * pad + cols * cellW + (cols - 1) * gap, userTop() + 2 * (cellH + gap) + 74);
    }

    ~WavePicker() override
    {
        model.changed.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        const auto current = model.get (more);
        const auto describe = [this] (int v) { return juce::String (peak::formatValue (more, v)) + " (" + label (v) + ")"; };
        CardStyle::paintCard (g, getLocalBounds(), "OSCILLATOR " + juce::String (osc) + juce::String::fromUTF8 ("  \xc2\xb7  MORE WAVEFORMS"),
                              current == startMore ? "Now: " + describe (current) : "Auditioning: " + describe (current) + "   was: " + describe (startMore));

        for (int i = 0; i < 70; ++i)
        {
            const auto v = valueAt (i);
            const auto r = cellBounds (i).toFloat();
            const bool selected = v == current, hovered = i == hover;
            g.setColour (hovered ? CardStyle::cellHover : CardStyle::cell);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (selected ? CardStyle::amber : CardStyle::line);
            g.drawRoundedRectangle (r.reduced (selected ? 1.0f : 0.5f), 5.0f, selected ? 2.0f : 1.0f);
            g.setFont (juce::FontOptions (10.0f));
            g.setColour (selected ? CardStyle::amber : CardStyle::dim);
            g.drawText (label (v), r.withWidth (30.0f).withTrimmedLeft (8.0f), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.0f));
            g.setColour (selected ? CardStyle::amber : CardStyle::text);
            g.drawFittedText (juce::String (peak::formatValue (more, v)), r.withTrimmedLeft (32.0f).withTrimmedRight (4.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
        }

        const auto y = userTop() - 32;
        g.setColour (CardStyle::line);
        g.fillRect (pad, y, getWidth() - 2 * pad, 1);
        g.setColour (CardStyle::blue);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("USER WAVETABLES  (names read from the Peak)", pad, y + 8, 400, 16, juce::Justification::centredLeft);

        paintInfo (g);

        g.setColour (CardStyle::dim);
        auto note = getLocalBounds().reduced (pad, 0).removeFromBottom (56).withWidth (300);
        g.drawFittedText ("Click or use the arrow keys to hear one on the Peak.\nDouble-click or Close keeps it;\n"
                          "Esc or Cancel goes back to where you started.",
                          note.withWidth (340), juce::Justification::centredLeft, 3);
    }

    void resized() override
    {
        // Under the factory wavetables, as wide as three columns: the name, then the line and its value.
        auto row = juce::Rectangle<int> (pad, gridTop + 10 * (cellH + gap) + 2, 3 * cellW + 2 * gap, 28);
        positionLabel.setBounds (row.removeFromLeft (132));
        position.setBounds (row);
        auto r = getLocalBounds().reduced (pad, 0).removeFromBottom (56).withSizeKeepingCentre (getWidth() - 2 * pad, 30);
        close.setBounds (r.removeFromRight (100));
        r.removeFromRight (10);
        cancel.setBounds (r.removeFromRight (100));
        r.removeFromRight (10);
        refresh.setBounds (r.removeFromRight (130));
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (cellAt (e.getPosition())); }

    // Shows the notes card for a wavetable at once, as if hovered (for snapshots).
    void previewInfo (int waveMore)
    {
        for (int i = 0; i < 70; ++i)
            if (valueAt (i) == waveMore)
                hover = i;
        showInfo = true;
    }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const auto i = cellAt (e.getPosition()); i >= 0)
            audition (i);
    }

    // Double-click: keep it and close, like Close.
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (cellAt (e.getPosition()) >= 0 && onClose)
            onClose();
    }

    // Arrow keys browse the wavetables (instead of patches and banks) while the picker is open.
    bool keyPressed (const juce::KeyPress& key) override
    {
        const int step = key.isKeyCode (juce::KeyPress::leftKey) ? -1 : key.isKeyCode (juce::KeyPress::rightKey) ? 1
                       : key.isKeyCode (juce::KeyPress::upKey)   ? -cols : key.isKeyCode (juce::KeyPress::downKey) ? cols : 0;
        if (step == 0)
            return false;
        int current = 0;
        for (int i = 0; i < 70; ++i)
            if (valueAt (i) == model.get (more))
                current = i;
        audition (juce::jlimit (0, 69, current + step));
        return true;
    }

private:
    static constexpr int cols = 6, cellW = 118, cellH = 30, gap = 6, pad = 18, gridTop = 64, positionRow = 40;

    static int valueAt (int i) { return i < 60 ? 4 + i : 64 + (i - 60); } // 60 factory, then 10 user
    static juce::String label (int v) { return v >= 64 ? "U" + juce::String (v - 63) : juce::String (v - 3).paddedLeft ('0', 2); }
    static int userTop() { return gridTop + 10 * (cellH + gap) + 40 + positionRow; }

    static juce::Rectangle<int> cellBounds (int i)
    {
        const auto top = i < 60 ? gridTop : userTop();
        const auto k = i < 60 ? i : i - 60;
        return { pad + (k % cols) * (cellW + gap), top + (k / cols) * (cellH + gap), cellW, cellH };
    }

    static int cellAt (juce::Point<int> p)
    {
        for (int i = 0; i < 70; ++i)
            if (cellBounds (i).contains (p))
                return i;
        return -1;
    }

    void setHover (int i)
    {
        if (std::exchange (hover, i) == i)
            return;
        // The notes card appears after a moment over a factory wavetable.
        showInfo = false;
        stopTimer();
        if (i >= 0 && peak::wavetableInfo (valueAt (i)) != nullptr)
            startTimer (450);
        repaint();
    }

    void timerCallback() override
    {
        stopTimer();
        showInfo = true;
        repaint();
    }

    // The listener's notes on the hovered wavetable (from the r/synthesizers table): what it
    // sounds like, its texture and what it suits. Below the cell, or above it near the bottom.
    void paintInfo (juce::Graphics& g) const
    {
        if (! showInfo || hover < 0)
            return;
        const auto v = valueAt (hover);
        const auto* info = peak::wavetableInfo (v);
        if (info == nullptr)
            return;

        constexpr float width = 300.0f, inset = 12.0f;
        juce::AttributedString text;
        text.setWordWrap (juce::AttributedString::byWord);
        text.append (juce::String (peak::formatValue (more, v)) + "\n", juce::FontOptions (14.0f, juce::Font::bold), CardStyle::blue);
        text.append (juce::String (info->description) + "\n\n", juce::FontOptions (13.0f), CardStyle::text);
        text.append ("Texture  ", juce::FontOptions (12.0f), CardStyle::dim);
        text.append (juce::String (info->texture) + "\n", juce::FontOptions (12.0f), CardStyle::text);
        text.append ("Usage  ", juce::FontOptions (12.0f), CardStyle::dim);
        text.append (info->usage, juce::FontOptions (12.0f), CardStyle::text);
        juce::TextLayout layout;
        layout.createLayout (text, width - 2.0f * inset);

        const auto cell = cellBounds (hover).toFloat();
        const auto h = layout.getHeight() + 2.0f * inset;
        auto box = juce::Rectangle<float> (width, h).withPosition (cell.getX(), cell.getBottom() + 6.0f);
        if (box.getBottom() > (float) getHeight() - 8.0f)
            box.setY (cell.getY() - 6.0f - h);
        box.setX (juce::jlimit (8.0f, (float) getWidth() - width - 8.0f, box.getX()));

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (box.translated (0.0f, 3.0f), 8.0f);
        g.setColour (CardStyle::cellHover);
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (CardStyle::border);
        g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
        layout.draw (g, box.reduced (inset));
    }

    void audition (int i)
    {
        // The wavetable goes to the Peak now (and the oscillator to More).
        model.set (more, valueAt (i));
        if (model.get (wave) != 4)
            model.set (wave, 4);
        repaint();
    }

    void goBack()
    {
        model.set (more, startMore);
        model.set (wave, startWave);
        model.set (shape, startShape);
        if (onClose)
            onClose();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override // e.g. user names read, or the knob turned
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        if (! position.isMouseButtonDown())
            position.setValue (model.get (shape), juce::dontSendNotification);
        repaint();
    }

    PeakModel& model;
    const int osc;
    const peak::ParamDef &more, &wave, &shape;
    const int startWave, startMore, startShape;
    int hover = -1;
    bool showInfo = false, updating = false;
    DrawerStyle::ResetSlider position { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label positionLabel { {}, "Wave Table Position" };
    juce::TextButton refresh { "Refresh user list" }, cancel { "Cancel" }, close { "Close" };
};
} // namespace

std::unique_ptr<juce::Component> FrontPanel::createMenuPage (PeakModel& model, const juce::String& name)
{
    // The Voice menu leaves out the mixer levels that have their own panel controls.
    static const juce::StringArray onPanel { "mix_osc1", "mix_osc2", "mix_osc3", "ring_1_2", "noise_level", "vca_gain" };
    std::vector<const peak::ParamDef*> params;
    for (const auto* section : menuSections (name))
        for (const auto& p : peak::allParams())
            if (std::strcmp (p.section, section) == 0 && ! (name == "Voice" && onPanel.contains (p.id)))
                params.push_back (&p);

    if (params.empty())
        return nullptr;
    return std::make_unique<MenuPage> (model, name + " menu", params, name == "Arp/Clock");
}

juce::Rectangle<float> FrontPanel::designBounds()
{
    const auto& p = PanelLayout::get().panel;
    return juce::Rectangle<float>::leftTopRightBottom (p[0] - woodWidth, p[1] - 6.0f, p[2] + woodWidth, p[3] + 6.0f);
}

FrontPanel::FrontPanel (PeakModel& m) : model (m)
{
    // The editor has no page or value controls: the screen takes their place.
    static const juce::StringArray replacedByScreen { "soft1", "soft2", "soft3", "page_prev", "page_next", "page_select",
                                                      "data_encoder", "value_down", "value_up", "value_label", "menu_patch",
                                                      "master_volume", "title_master", // the Peak has no MIDI master volume
                                                      "patch_encoder",                  // Patch -/+ and Bank -/+ (and the arrow keys) instead
                                                      "vca_frame" };                    // the grey box round VCA Gain
    for (const auto& e : PanelLayout::get().elements)
    {
        if (replacedByScreen.contains (e.id))
            continue;
        Control c;
        c.e = &e;
        if (e.type == PanelElement::display)
        {
            auto& screen = changedElements.emplace_back (e);
            screen.x = 402.0f; // from the soft buttons' left edge to the Value buttons' right edge
            screen.y = 238.0f;
            screen.x1 = 988.0f;
            screen.y1 = 470.0f;
            c.e = &screen;
        }
        else if (std::strcmp (e.id, "patch_down") == 0 || std::strcmp (e.id, "patch_up") == 0 || std::strcmp (e.id, "patch_pm_label") == 0)
        {
            // Below Bank -/+, which is level with the top of the screen.
            auto& moved = changedElements.emplace_back (e);
            moved.y -= 124.0f;
            c.e = &moved;
        }
        else if (juce::String (e.bind).startsWith ("menu:") && e.type == PanelElement::button)
        {
            // The menu buttons line up with the screen's left edge.
            auto& moved = changedElements.emplace_back (e);
            moved.x -= 107.6f;
            c.e = &moved;
        }
        else if (juce::String (e.id).startsWith ("anim") || std::strcmp (e.id, "title_animate") == 0)
        {
            auto& moved = changedElements.emplace_back (e);
            moved.y -= 193.6f; // up into Master's place
            c.e = &moved;
        }
        else if (std::strcmp (e.id, "audition") == 0)
        {
            auto& revert = changedElements.emplace_back (e);
            revert.label = "Revert";
            revert.bind = "action:revert";
            c.e = &revert;
        }
        const juce::String bind (c.e->bind); // after any override above

        if (bind.startsWith ("menu:"))
            c.menu = bind.fromFirstOccurrenceOf ("menu:", false, false);
        else if (bind.startsWith ("action:"))
            c.action = bind.fromFirstOccurrenceOf ("action:", false, false);
        else if (bind.startsWith ("cycle:"))
        {
            c.cycle = true;
            c.source = parseSource (bind.fromFirstOccurrenceOf ("cycle:", false, false));
        }
        else if (e.type == PanelElement::led)
            c.source = parseSource (bind);
        else if (bind.isNotEmpty())
        {
            for (const auto& alt : juce::StringArray::fromTokens (bind, "|", ""))
            {
                Alternative a;
                a.param = peak::findParam (alt.upToFirstOccurrenceOf ("@", false, false).toStdString());
                if (alt.contains ("@"))
                {
                    a.when = parseSource (alt.fromFirstOccurrenceOf ("@", false, false).upToFirstOccurrenceOf ("=", false, false));
                    a.whenValue = alt.fromLastOccurrenceOf ("=", false, false).getIntValue();
                }
                if (a.param != nullptr)
                    c.alternatives.push_back (a);
            }
        }

        controls.push_back (std::move (c));
    }

    // Animate and Library to the right of Arp/Clock and Settings.
    for (const auto& added : { PanelElement { PanelElement::button, "menu_animate", 769.8f, 134.7f, 0.0f, 0.0f, 62.0f, 26.0f, "Animate", "menu:Animate", 0, false, "white" },
                               PanelElement { PanelElement::button, "menu_library", 769.9f, 193.7f, 0.0f, 0.0f, 62.0f, 26.0f, "Library", "menu:Library", 0, false, "white" } })
    {
        Control c;
        c.e = &changedElements.emplace_back (added);
        c.menu = juce::String (added.bind).fromFirstOccurrenceOf ("menu:", false, false);
        controls.push_back (std::move (c));
    }

    // Bank -/+ above Patch -/+, the buttons' tops level with the screen's.
    for (const auto& added : { PanelElement { PanelElement::button, "bank_down", 251.9f, 251.0f, 0.0f, 0.0f, 62.0f, 26.0f, "", "action:bank_down", 0, false, "dark" },
                               PanelElement { PanelElement::button, "bank_up", 336.3f, 251.1f, 0.0f, 0.0f, 62.0f, 26.0f, "", "action:bank_up", 0, false, "dark" },
                               PanelElement { PanelElement::text, "bank_pm_label", 290.7f, 284.4f, 0.0f, 0.0f, 12.0f, 0.0f, "–      Bank      +", "", 0, false, "centre" } })
    {
        Control c;
        c.e = &changedElements.emplace_back (added);
        c.action = juce::String (added.bind).fromFirstOccurrenceOf ("action:", false, false);
        controls.push_back (std::move (c));
    }

    model.changed.addChangeListener (this);
    setOpaque (true);
}

FrontPanel::~FrontPanel()
{
    model.changed.removeChangeListener (this);
}

FrontPanel::Source FrontPanel::parseSource (const juce::String& name)
{
    Source s;
    if (name == "ui:modenv_select")
        s.ui = &modEnvSelect;
    else
        s.param = peak::findParam (name.toStdString());
    return s;
}

int FrontPanel::read (const Source& s) const
{
    return s.param != nullptr ? model.get (*s.param) : s.ui != nullptr ? *s.ui : -1;
}

const peak::ParamDef* FrontPanel::resolve (const Control& c) const
{
    for (const auto& a : c.alternatives)
        if (! a.when.valid() || read (a.when) == a.whenValue)
            return a.param;
    return c.alternatives.empty() ? nullptr : c.alternatives.front().param;
}

void FrontPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    message.clear();
    const auto* p = model.getLastTouched();
    if (p == nullptr)
        shownParam = nullptr; // a new patch: show it at once
    else if (p != shownParam || model.get (*p) != shownValue)
    {
        shownParam = p;
        shownValue = model.get (*p);
        shownSince = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (30);
    }
    repaint();
}

constexpr double parameterHoldMs = 1500.0, parameterFadeMs = 600.0;

float FrontPanel::parameterAlpha() const
{
    if (shownParam == nullptr)
        return 0.0f;
    if (dragParam == shownParam) // held while the control is being dragged
        return 1.0f;
    const auto age = juce::Time::getMillisecondCounterHiRes() - shownSince;
    return (float) juce::jlimit (0.0, 1.0, 1.0 - (age - parameterHoldMs) / parameterFadeMs);
}

void FrontPanel::timerCallback()
{
    if (parameterAlpha() <= 0.0f)
    {
        stopTimer();
        shownParam = nullptr;
    }
    repaint();
}

//==============================================================================
juce::AffineTransform FrontPanel::designToScreen() const
{
    const auto d = designBounds();
    const auto scale = juce::jmin ((float) getWidth() / d.getWidth(), (float) getHeight() / d.getHeight());
    const auto ox = ((float) getWidth() - d.getWidth() * scale) * 0.5f, oy = ((float) getHeight() - d.getHeight() * scale) * 0.5f;
    return juce::AffineTransform::translation (-d.getX(), -d.getY()).scaled (scale).translated (ox, oy);
}

juce::Point<float> FrontPanel::toDesign (juce::Point<float> p) const
{
    return p.transformedBy (designToScreen().inverted());
}

FrontPanel::Control* FrontPanel::controlAt (juce::Point<float> p)
{
    for (auto& c : controls)
    {
        const auto& e = *c.e;
        const juce::Point<float> centre (e.x, e.y);
        switch (e.type)
        {
            case PanelElement::knob:
                if (p.getDistanceFrom (centre) < knobSkirt * (e.flag ? bigScale : 1.0f) + 4.0f)
                    return &c;
                break;
            case PanelElement::button:
                if (juce::Rectangle<float> (e.w + 8, e.h + 8).withCentre (centre).contains (p))
                    return &c;
                break;
            case PanelElement::led:
                if (juce::Rectangle<float> (60, 18).withPosition (e.x - 10, e.y - 9).contains (p) && c.source.valid())
                    return &c;
                break;
            case PanelElement::fader:
                if (juce::Rectangle<float>::leftTopRightBottom (e.x - 30, e.y - 20, e.x + 30, e.y1 + 20).contains (p))
                    return &c;
                break;
            case PanelElement::arrow:
                if (p.getDistanceFrom (centre) < 14)
                    return &c;
                break;
            case PanelElement::text:
            case PanelElement::bracket:
            case PanelElement::display:
            case PanelElement::frame:
                break;
        }
    }
    return nullptr;
}

//==============================================================================
void FrontPanel::setParam (const peak::ParamDef& p, int value)
{
    model.set (p, value);
    model.setLastTouched (&p);
}

void FrontPanel::mouseDown (const juce::MouseEvent& ev)
{
    if (starArea.contains (toDesign (ev.position)) && parameterAlpha() <= 0.0f && message.isEmpty())
        return toggleFavourite();
    if (helpButtonArea().contains (toDesign (ev.position)))
        return showHelp();
    if (categoryArea.contains (toDesign (ev.position)) && parameterAlpha() <= 0.0f && message.isEmpty() && model.hasPatch())
        return chooseCategory();
    if (ev.mods.isPopupMenu())
    {
        // Right-click: modulate this control (or send its source somewhere), as in the TEO-5 editor.
        if (auto* c = controlAt (toDesign (ev.position)))
            if (const auto* p = c->source.param != nullptr && c->alternatives.empty() ? c->source.param : resolve (*c))
                ModAssign::showMenu (model, *p);
        return;
    }
    dragging = controlAt (toDesign (ev.position));
    dragParam = nullptr;
    if (dragging == nullptr)
        return;

    const auto& e = *dragging->e;
    if (std::strcmp (e.id, "anim1") == 0 || std::strcmp (e.id, "anim2") == 0)
    {
        // Animate is on while pressed. With Hold on, a press latches it and a second press releases it.
        const auto& p = *peak::findParam (e.id[4] == '1' ? "animate1_hold" : "animate2_hold");
        const auto on = ! (animateHold && model.get (p) != 0);
        setParam (p, on ? p.max : p.min);
        if (on && ! animateHold)
            animatePressed = &p;
        return;
    }
    if (std::strcmp (e.id, "anim_hold") == 0)
    {
        animateHold = ! animateHold;
        if (! animateHold) // Hold off releases any latched Animate
            for (const auto* id : { "animate1_hold", "animate2_hold" })
                if (const auto& p = *peak::findParam (id); model.get (p) != 0)
                    setParam (p, p.min);
        repaint();
        return;
    }
    if (e.type == PanelElement::knob || e.type == PanelElement::fader)
    {
        // The data encoder edits whatever the display is showing, as on the hardware.
        dragParam = dragging->action == "data_encoder" ? model.getLastTouched() : resolve (*dragging);
        if (dragParam == nullptr)
        {
            if (dragging->action.isNotEmpty() && dragging->action != "data_encoder")
                press (*dragging);
            return;
        }
        dragStartValue = model.get (*dragParam);
        dragStartY = ev.position.y;
        model.setLastTouched (dragParam);
        if (e.type == PanelElement::fader)
            mouseDrag (ev);
    }
    else
    {
        press (*dragging);
    }
}

void FrontPanel::mouseDrag (const juce::MouseEvent& ev)
{
    if (dragging == nullptr || dragParam == nullptr)
        return;

    const auto& p = *dragParam;
    const auto range = (float) (p.max - p.min);

    if (dragging->e->type == PanelElement::fader)
    {
        const auto& e = *dragging->e;
        const auto y = toDesign (ev.position).y;
        const auto norm = juce::jlimit (0.0f, 1.0f, (e.y1 - y) / (e.y1 - e.y));
        setParam (p, p.min + juce::roundToInt (norm * range));
        return;
    }

    // Knobs: vertical drag, 250 px for the full range (Shift for 5x finer).
    const auto pixels = ev.mods.isShiftDown() ? 1250.0f : 250.0f;
    const auto delta = (dragStartY - ev.position.y) / pixels * range;
    setParam (p, juce::jlimit (p.min, p.max, dragStartValue + juce::roundToInt (delta)));
}

void FrontPanel::mouseUp (const juce::MouseEvent&)
{
    if (animatePressed != nullptr)
        setParam (*animatePressed, animatePressed->min);
    animatePressed = nullptr;
    if (dragParam != nullptr && dragParam == shownParam)
        shownSince = juce::Time::getMillisecondCounterHiRes(); // the screen holds a moment after letting go, then fades
    dragging = nullptr;
    dragParam = nullptr;
}

void FrontPanel::mouseDoubleClick (const juce::MouseEvent& ev)
{
    if (nameArea.contains (toDesign (ev.position)) && parameterAlpha() <= 0.0f && message.isEmpty() && model.hasPatch())
        return renamePatch();
    if (auto* c = controlAt (toDesign (ev.position)); c != nullptr && (c->e->type == PanelElement::knob || c->e->type == PanelElement::fader))
        if (const auto* p = resolve (*c))
            setParam (*p, model.getStored (*p)); // back to the patch's saved value
}

void FrontPanel::mouseWheelMove (const juce::MouseEvent& ev, const juce::MouseWheelDetails& wheel)
{
    auto* c = controlAt (toDesign (ev.position));
    if (c == nullptr || (c->e->type != PanelElement::knob && c->e->type != PanelElement::fader))
        return;

    if (const auto* p = c->action == "data_encoder" ? model.getLastTouched() : resolve (*c))
    {
        const auto step = c->action == "data_encoder" ? 1 : juce::jmax (1, (p->max - p->min) / 100);
        const auto dir = wheel.deltaY > 0 ? 1 : -1;
        setParam (*p, juce::jlimit (p->min, p->max, model.get (*p) + dir * step));
    }
}

void FrontPanel::press (Control& c)
{
    const auto& e = *c.e;

    if (c.menu == "Patch") // as on the Peak: the display shows the current patch
    {
        message.clear();
        model.setLastTouched (nullptr);
        return;
    }
    if (c.menu == "Mod" || c.menu == "Settings" || c.menu == "Library") // these have their own tabs
    {
        if (onShowTab)
            onShowTab (c.menu == "Mod" ? "Mod Matrix" : c.menu);
        return;
    }
    if (c.menu.isNotEmpty())
    {
        openMenu (c.menu, e);
        return;
    }

    if (c.cycle || (e.type == PanelElement::led && c.source.valid()))
    {
        // Cycle buttons step through the selector's values; clicking an LED picks its value directly.
        auto value = e.type == PanelElement::led ? e.value : read (c.source) + 1;
        if (c.source.param != nullptr)
        {
            const auto& p = *c.source.param;
            if (value > p.max || value < p.min)
                value = p.min;
            const auto before = model.get (p);
            setParam (p, value);
            // An oscillator's Wave set to More (or More clicked again): choose the wavetable.
            if (value == 4 && std::string_view (p.id).size() == 9 && std::string_view (p.id).substr (0, 3) == "osc"
                && std::string_view (p.id).substr (4) == "_wave")
                showCard (createWavePicker (model, p.id[3] - '0', before));
        }
        else if (c.source.ui != nullptr)
        {
            *c.source.ui = e.type == PanelElement::led ? value : (value > 1 ? 0 : value);
            repaint();
        }
        return;
    }

    if (! c.alternatives.empty()) // toggles: Glide On, Arp On, Key Latch, Delay Sync, Bypass, Animate Hold
    {
        if (const auto* p = resolve (c))
            setParam (*p, model.get (*p) != 0 ? p->min : p->max);
        return;
    }

    if (c.action == "value_up")
        stepLastTouched (1);
    else if (c.action == "value_down")
        stepLastTouched (-1);
    else if (c.action == "page_next")
        movePage (1);
    else if (c.action == "page_prev")
        movePage (-1);
    else if (c.action.startsWith ("soft"))
        movePage (c.action.getLastCharacters (1).getIntValue() - 2); // soft buttons pick display rows 1-3
    else if (c.action == "compare")
        model.toggleCompare();
    else if (c.action == "revert")
    {
        if (! model.isModified())
            showMessage ("No changes to revert");
        else if (! model.revert())
            showMessage ("No MIDI output to the Peak");
    }
    else if (c.action == "bank_up" || c.action == "bank_down")
        stepBank (c.action == "bank_up" ? 1 : -1);
    else if (c.action == "save")
    {
        if (! model.hasPatch())
            showMessage ("No patch to save");
        else if (onSave)
            onSave();
    }
    else if (c.action == "patch_up" || c.action == "patch_down")
        stepPatch (c.action == "patch_up" ? 1 : -1);
    else if (c.action == "initialise")
    {
        // As the Peak's own Initialise: the Init Patch becomes the current sound (nothing saved).
        if (! model.audition (DrawerStyle::emptyPatch(), "Init Patch (not saved)"))
            showMessage ("No MIDI output to the Peak");
    }
    else if (c.action.isNotEmpty())
        showMessage ("Not available yet");
}

juce::Rectangle<float> FrontPanel::helpButtonArea()
{
    const auto& p = PanelLayout::get().panel;
    return { p[0] + 18.0f, p[1] + 14.0f, 36.0f, 36.0f };
}

// PEAK as on the hardware, right-aligned at `right`, letters `h` high from `top`. Traced from
// the reference photo (docs/reference/peak-front-panel.png, measured at 6x: letters span
// x 65-1242, y 56-266 of that enlargement).
juce::Path FrontPanel::peakLogo (float right, float top, float h)
{
    juce::Path logo;
    logo.setUsingNonZeroWinding (false); // the A's counter is a hole
    const auto poly = [&] (std::initializer_list<juce::Point<float>> points) {
        auto it = points.begin();
        logo.startNewSubPath (*it);
        for (++it; it != points.end(); ++it)
            logo.lineTo (*it);
        logo.closeSubPath();
    };

    // P: a slanted top bar into a round bowl; the counter is a slot open to the left; the stem hangs below.
    logo.startNewSubPath (106, 56);
    logo.lineTo (259, 56);
    logo.addCentredArc (259, 134, 78, 78, 0.0f, 0.0f, juce::MathConstants<float>::pi);
    logo.lineTo (128, 212);
    logo.lineTo (128, 262);
    logo.lineTo (65, 262);
    logo.lineTo (65, 155);
    logo.lineTo (269, 155);
    logo.lineTo (269, 113);
    logo.lineTo (64, 113);
    logo.closeSubPath();

    // E: an upright with top and bottom bars; the middle bar stands apart. Bar ends are slanted.
    poly ({ { 368, 56 }, { 610, 56 }, { 566, 113 }, { 435, 113 }, { 435, 213 }, { 609, 213 }, { 571, 262 }, { 368, 262 } });
    poly ({ { 464, 140 }, { 596, 140 }, { 559, 190 }, { 464, 190 } });

    // A: a flat-topped triangle with a triangular counter.
    poly ({ { 752, 56 }, { 800, 56 }, { 947, 266 }, { 602, 266 } });
    poly ({ { 776, 126 }, { 838, 212 }, { 714, 212 } });

    // K: an upright, and a chevron standing apart from it.
    poly ({ { 967, 56 }, { 1033, 56 }, { 1033, 266 }, { 967, 266 } });
    poly ({ { 1152, 56 }, { 1236, 56 }, { 1112, 160 }, { 1242, 266 }, { 1146, 266 }, { 1057, 190 }, { 1057, 134 } });

    const auto scale = h / 210.0f;
    logo.applyTransform (juce::AffineTransform::translation (-65.0f, -56.0f).scaled (scale).translated (right - 1177.0f * scale, top));
    return logo;
}

void FrontPanel::showHelp()
{
    auto text = std::make_unique<juce::TextEditor>();
    text->setMultiLine (true);
    text->setReadOnly (true);
    text->setCaretVisible (false);
    text->setScrollbarsShown (true);
    text->setFont (juce::FontOptions (14.0f));
    text->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1e1f22));
    text->setColour (juce::TextEditor::textColourId, juce::Colour (0xffe6e6e6));
    text->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text->setText (juce::String::fromUTF8 (
        "FRONT PANEL HELP\n\n"
        "Knobs and sliders\n"
        "\xe2\x80\xa2 Drag up/down (or scroll) to change a value. Changes go straight to the Peak.\n"
        "\xe2\x80\xa2 Double-click a knob, slider or menu value to put it back as saved in the patch.\n"
        "\xe2\x80\xa2 The screen shows the parameter, its value and (in brackets) the saved value, then fades back to the patch.\n"
        "\xe2\x80\xa2 The LEDs and the small buttons under them choose waves, ranges and sources. Click an LED to pick it directly.\n"
        "\xe2\x80\xa2 Turning a knob on the Peak moves it here too.\n\n"
        "The screen\n"
        "\xe2\x80\xa2 Top left: the patch's slot (e.g. B063). Top right: its category. Middle: its name.\n"
        "\xe2\x80\xa2 Click the star to add the patch to (or remove it from) Favourites.\n"
        "\xe2\x80\xa2 Double-click the name to rename the patch, then overwrite it or save it somewhere new.\n"
        "\xe2\x80\xa2 \"Edited\" (amber) means the sound differs from the saved patch.\n\n"
        "Patch\n"
        "\xe2\x80\xa2 Patch \xe2\x80\x93/+ (or the Left/Right arrow keys) steps through patches: Peak banks A\xe2\x80\x93" "D, then My Banks.\n"
        "\xe2\x80\xa2 Bank \xe2\x80\x93/+ (or the Up/Down arrow keys) moves to the same position in the previous/next bank.\n"
        "\xe2\x80\xa2 Save opens the Library to choose where the patch goes; click a slot to hear what's there, then Confirm.\n"
        "\xe2\x80\xa2 Compare switches between your edit and the saved patch. Revert throws your edits away.\n"
        "\xe2\x80\xa2 Initialise loads the Init Patch (nothing is saved until you Save).\n\n"
        "Animate\n"
        "\xe2\x80\xa2 Animate 1 and 2 are on while you hold the mouse down.\n"
        "\xe2\x80\xa2 Turn Hold on first to latch them; turn Hold off to release.\n"
        "\xe2\x80\xa2 What they do is set in the Mod Matrix (sources Animate 1/2) and their timing in the Animate menu.\n\n"
        "Menu buttons\n"
        "\xe2\x80\xa2 Osc, Env, LFO, Arp/Clock, Animate, Voice and FX open the settings that have no knob on the panel.\n"
        "\xe2\x80\xa2 Mod opens the Mod Matrix tab, Settings the Settings tab and Library the Library tab.\n"
        "\xe2\x80\xa2 The small arrows (\xe2\x96\xb6) by the Oscillator, VCA and Filter sections open their menus too.\n\n"
        "Playing\n"
        "\xe2\x80\xa2 The keyboard under the panel plays the Peak; your computer keys play it too while the keyboard has focus.\n"
        "\xe2\x80\xa2 A controller keyboard can be chosen in the MIDI settings and is passed through to the Peak.\n\n"
        "Connection\n"
        "\xe2\x80\xa2 When the editor opens it asks the Peak for its current sound and settings.\n"
        "\xe2\x80\xa2 The Peak's MIDI channel and CC/NRPN setting must allow receiving (Settings > MIDI)."));
    text->setSize (560, 520);
    const auto area = helpButtonArea().transformedBy (designToScreen()).toNearestInt();
    juce::CallOutBox::launchAsynchronously (std::move (text), localAreaToGlobal (area), nullptr);
}

bool FrontPanel::drawGraph (juce::Graphics& g, juce::Rectangle<float> inner, float lineH, float alpha) const
{
    const juce::String id (shownParam->id);
    const auto value = [this] (const juce::String& paramId) { return model.get (*peak::findParam (paramId.toStdString())); };

    std::function<void (juce::Graphics&, juce::Rectangle<float>)> paint;
    if (id == "filter_frequency" || id == "filter_resonance" || id == "filter_slope" || id == "filter_shape")
    {
        const DisplayGraph::Filter f { value ("filter_frequency"), value ("filter_resonance"), value ("filter_shape"), value ("filter_slope") != 0 };
        paint = [f] (juce::Graphics& gg, juce::Rectangle<float> area) { DisplayGraph::drawFilter (gg, area, f, colours::oled); };
    }
    else
    {
        // Amp Envelope or Mod Envelope 1/2: one of its time or level stages.
        const auto env = id.upToFirstOccurrenceOf ("_", false, false);
        const auto stage = id.fromFirstOccurrenceOf ("_", false, false);
        static const juce::StringArray stages { "delay", "attack", "holdtime", "decay", "sustain", "release" };
        if (! (env == "ampenv" || env == "modenv1" || env == "modenv2") || ! stages.contains (stage))
            return false;
        DisplayGraph::Envelope e;
        e.delay = value (env + "_delay");
        e.attack = value (env + "_attack");
        e.hold = value (env + "_holdtime");
        e.decay = value (env + "_decay");
        e.sustain = value (env + "_sustain");
        e.release = value (env + "_release");
        e.focus = stage == "delay" ? 'y' : stage == "holdtime" ? 'h' : stage[0];
        paint = [e] (juce::Graphics& gg, juce::Rectangle<float> area) { DisplayGraph::drawEnvelope (gg, area, e, colours::oled); };
    }

    g.beginTransparencyLayer (alpha);
    auto area = inner;
    auto line = area.removeFromBottom (lineH * 1.4f);
    paint (g, area.reduced (0.0f, 2.0f));

    // "AmpEnv Decay        90 (68)": the name at the left, the value and saved value at the right,
    // smaller if they wouldn't fit (the name already says which envelope).
    const auto& p = *shownParam;
    const auto v = juce::String (peak::formatValue (p, model.get (p)));
    const auto saved = p.isStored() ? " (" + juce::String (peak::formatValue (p, model.getStored (p))) + ")" : juce::String();
    const auto chars = juce::String (p.name).length() + v.length() + saved.length() + 3;
    const auto size = juce::jmin (lineH, line.getWidth() / (float) chars / 0.6f);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain)));
    g.setColour (colours::oled);
    g.drawText (juce::String (p.name), line, juce::Justification::centredLeft);
    g.drawText (v + saved, line, juce::Justification::centredRight);
    g.endTransparencyLayer();
    return true;
}

void FrontPanel::chooseCategory()
{
    const auto& param = *peak::findParam ("patch_category");
    const auto current = model.get (param);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Category");
    const auto& names = peak::categoryNames();
    for (int i = 0; i < (int) names.size(); ++i)
        menu.addItem (names[(size_t) i], true, i == current, [this, i] { setCategoryAndSave (i); });
    const auto area = categoryArea.transformedBy (designToScreen()).toNearestInt();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (area)));
}

void FrontPanel::setCategoryAndSave (int category)
{
    // The category is saved where the patch lives straight away: into its Peak slot (the stored
    // version with the new category, so other edits stay unsaved) or its My Banks entry.
    const auto& param = *peak::findParam ("patch_category");
    if (category == model.get (param))
        return;
    const auto home = DrawerStyle::loadedPatchKey (model);
    auto& library = model.getLibrary();
    if (home.startsWith ("p:"))
    {
        const peak::Patch::Location where { home.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                            home.fromLastOccurrenceOf (":", false, false).getIntValue() };
        auto stored = library.syncedSlot (where).value_or (model.getReference());
        stored.set (param, category);
        bool ok = false;
        const auto result = SlotWriter::writePatch (model, stored, where, ok);
        if (ok)
            model.setSavedValue (param, category);
        showMessage (ok ? "Category saved to " + juce::String (where.name()) : result);
    }
    else if (home.startsWith ("l:"))
    {
        library.checkpoint();
        library.setCategory (home.substring (2), category);
        model.setSavedValue (param, category);
        showMessage ("Category saved in My Banks");
    }
    else
    {
        // Not stored anywhere yet: it's an edit until the patch is saved.
        setParam (param, category);
        showMessage ("Not stored yet: use Save to keep it");
    }
}

void FrontPanel::toggleFavourite()
{
    if (! model.hasPatch())
        return;
    auto& library = model.getLibrary();
    library.checkpoint();
    const auto home = DrawerStyle::loadedPatchKey (model);
    if (const auto* e = home.startsWith ("l:") ? library.find (home.substring (2)) : nullptr)
        library.setFavourite (e->id, ! e->favourite);
    else
        library.setPeakFavourite (model.getReference(), ! library.isPeakFavourite (model.getReference()));
    repaint();
}

void FrontPanel::renamePatch()
{
    const auto oldName = juce::String (model.getPatch().name());
    auto* w = new juce::AlertWindow ("Rename patch", "A new name (up to 16 characters):", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", oldName);
    if (auto* field = w->getTextEditor ("name"))
        field->setInputRestrictions (16);
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, oldName] (int result) {
        const auto newName = w->getTextEditorContents ("name").trim();
        if (result != 1 || newName.isEmpty() || newName == oldName)
            return;
        model.rename (newName);

        // Then: overwrite where it came from, or save somewhere else (the Save picker).
        const auto home = DrawerStyle::loadedPatchKey (model);
        const peak::Patch::Location slot { home.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                           home.fromLastOccurrenceOf (":", false, false).getIntValue() };
        juce::String homeName;
        if (home.startsWith ("p:"))
            homeName = juce::String (slot.name());
        else if (const auto* e = home.startsWith ("l:") ? model.getLibrary().find (home.substring (2)) : nullptr)
            homeName = e->bank;
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Save \"" + newName + "\"")
                           .withMessage (homeName.isNotEmpty() ? "Overwrite the patch in " + homeName + " with the renamed one, or save it to a new destination?"
                                                               : juce::String ("Save the renamed patch to a destination?"))
                           .withAssociatedComponent (this);
        if (homeName.isNotEmpty())
            options = options.withButton (juce::MessageBoxButtonOptions ("Overwrite " + homeName, 1).withRole (juce::MessageBoxButtonRole::destroy));
        options = options.withButton (juce::MessageBoxButtonOptions (juce::String::fromUTF8 ("Save to a new destination\xe2\x80\xa6"), 2)
                                          .withRole (juce::MessageBoxButtonRole::normal))
                      .withButton (juce::MessageBoxButtonOptions ("Not now", 0).withRole (juce::MessageBoxButtonRole::cancel));
        juce::AlertWindow::showAsync (options, [this, home, slot] (int choice) {
            const bool overwrite = choice == 1, elsewhere = choice == 2;
            if (overwrite && home.startsWith ("p:"))
            {
                bool ok = false;
                showMessage (SlotWriter::write (model, slot, ok));
            }
            else if (overwrite)
            {
                auto& library = model.getLibrary();
                library.checkpoint();
                library.replacePatch (home.substring (2), model.getPatch());
                model.markSaved (std::nullopt, "My Banks: " + juce::String (model.getPatch().name()));
                model.setLoadedFrom (home);
                showMessage ("Saved in My Banks");
            }
            else if (elsewhere && onSave)
                onSave();
            // "Not now": the new name stays as an unsaved edit (Revert undoes it).
        });
    }), true);
}

//==============================================================================
// Patch and bank stepping: Peak banks A-D first, then My Banks in order, as one list.
namespace
{
struct Place
{
    int bank = 0;  // 0-3: Peak A-D; 4+: My Banks (getBanks() index + 4)
    int index = 0; // slot, or position in the My Banks bank
};
}

static std::vector<const PatchLibrary::Entry*> entriesOf (const PatchLibrary& library, int bank)
{
    return bank >= 4 && bank - 4 < library.getBanks().size() ? library.entriesIn (library.getBanks()[bank - 4])
                                                               : std::vector<const PatchLibrary::Entry*> {};
}

static int bankSize (const PatchLibrary& library, int bank)
{
    return bank < 4 ? 128 : (int) entriesOf (library, bank).size();
}

static Place currentPlace (const PeakModel& model)
{
    const auto& library = model.getLibrary();
    // Loaded from My Banks (and still that sound): its bank and position.
    if (const auto& from = model.getLoadedFrom(); from.startsWith ("l:"))
        if (const auto* e = library.find (from.substring (2)); e != nullptr && e->patch.sameSound (model.getReference()))
        {
            const auto bank = library.getBanks().indexOf (e->bank);
            const auto list = entriesOf (library, bank + 4);
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i] == e)
                    return { bank + 4, i };
        }
    const auto where = model.getOriginalLocation().value_or (peak::Patch::Location {});
    return { where.bank, where.slot };
}

void FrontPanel::goTo (int bank, int index)
{
    if (bank < 4)
    {
        if (! model.selectPeakSlot ({ bank, index }))
            showMessage ("No MIDI output to the Peak");
        return;
    }
    const auto list = entriesOf (model.getLibrary(), bank);
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    const auto& e = *list[(size_t) index];
    if (! model.audition (e.patch, "My Banks: " + juce::String (e.patch.name())))
        showMessage ("No MIDI output to the Peak");
    model.setLoadedFrom ("l:" + e.id);
}

void FrontPanel::stepPatch (int delta)
{
    // Through every patch: A000 ... D127, then each My Banks bank in turn, wrapping round.
    const auto& library = model.getLibrary();
    const int banks = 4 + library.getBanks().size();
    auto [bank, index] = currentPlace (model);
    for (int guard = 0; guard < 4096; ++guard)
    {
        index += delta;
        while (index >= bankSize (library, bank))
        {
            bank = (bank + 1) % banks;
            index = 0;
        }
        while (index < 0)
        {
            bank = (bank - 1 + banks) % banks;
            index = bankSize (library, bank) - 1;
        }
        if (bankSize (library, bank) > 0)
            return goTo (bank, index);
    }
}

void FrontPanel::stepBank (int delta)
{
    // To the same position in the next bank with patches in it (or its last patch).
    const auto& library = model.getLibrary();
    const int banks = 4 + library.getBanks().size();
    const auto [current, index] = currentPlace (model);
    for (int step = 1; step < banks; ++step)
    {
        const auto bank = ((current + delta * step) % banks + banks) % banks;
        if (const auto size = bankSize (library, bank); size > 0)
            return goTo (bank, juce::jmin (index, size - 1));
    }
}

void FrontPanel::stepLastTouched (int delta)
{
    if (const auto* p = model.getLastTouched())
        setParam (*p, juce::jlimit (p->min, p->max, model.get (*p) + delta));
}

void FrontPanel::movePage (int delta)
{
    // Moves the display's selection to a neighbouring parameter in the same section.
    const auto& all = peak::allParams();
    const auto* p = model.getLastTouched();
    if (p == nullptr)
        return;

    auto i = peak::indexOf (*p) + delta;
    if (i >= 0 && i < (int) all.size() && std::strcmp (all[(size_t) i].section, p->section) == 0)
        model.setLastTouched (&all[(size_t) i]);
}

void FrontPanel::showMessage (const juce::String& text)
{
    message = text;
    repaint();
}

void FrontPanel::openMenu (const juce::String& name, const PanelElement& from)
{
    if (name == "Settings")
    {
        showMessage ("Global settings stay on the Peak");
        return;
    }

    auto page = createMenuPage (model, name);
    if (page == nullptr)
        return;

    openMenuName = name;
    repaint();
    showCard (std::move (page));
}

void FrontPanel::showCard (std::unique_ptr<juce::Component> card)
{
    auto* closable = dynamic_cast<ClosableCard*> (card.get());
    overlay = std::make_unique<CardStyle::Overlay> (std::move (card), [this] {
        overlay.reset();
        openMenuName.clear();
        repaint();
    });
    if (closable != nullptr)
    {
        closable->onClose = [this] { if (overlay) overlay->close(); };
        if (closable->onEscape)
            overlay->onEscape = closable->onEscape;
    }
    addAndMakeVisible (*overlay);
    overlay->setBounds (getLocalBounds());
    overlay->grabKeyboardFocus();
}

void FrontPanel::closeCard()
{
    if (overlay)
        overlay->close();
}

void FrontPanel::resized()
{
    if (overlay)
        overlay->setBounds (getLocalBounds());
}

std::unique_ptr<juce::Component> FrontPanel::createWavePicker (PeakModel& model, int oscillator, int waveBefore, int previewWave)
{
    auto picker = std::make_unique<WavePicker> (model, oscillator, waveBefore);
    if (previewWave >= 0)
        picker->previewInfo (previewWave);
    return picker;
}

//==============================================================================
void FrontPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141414));
    g.addTransform (designToScreen());

    drawBackground (g);

    for (const auto& c : controls)
    {
        switch (c.e->type)
        {
            case PanelElement::knob:    drawKnob (g, c); break;
            case PanelElement::button:  drawButton (g, c); break;
            case PanelElement::led:     drawLed (g, c); break;
            case PanelElement::fader:   drawFader (g, c); break;
            case PanelElement::display: drawDisplay (g, *c.e); break;
            case PanelElement::text:
            case PanelElement::arrow:
            case PanelElement::bracket:
            case PanelElement::frame: break; // drawn with the background
        }
    }
}

void FrontPanel::drawBackground (juce::Graphics& g) const
{
    const auto& layout = PanelLayout::get();
    const auto face = juce::Rectangle<float>::leftTopRightBottom (layout.panel[0], layout.panel[1], layout.panel[2], layout.panel[3]);
    const auto d = designBounds();

    // Walnut end cheeks.
    for (const auto& cheek : { juce::Rectangle<float>::leftTopRightBottom (d.getX(), d.getY(), face.getX() - 2, d.getBottom()),
                               juce::Rectangle<float>::leftTopRightBottom (face.getRight() + 2, d.getY(), d.getRight(), d.getBottom()) })
    {
        g.setGradientFill (juce::ColourGradient (colours::wood, cheek.getX(), 0, colours::woodDark, cheek.getRight(), 0, false));
        g.fillRoundedRectangle (cheek, 6.0f);
        g.setColour (colours::woodDark.withAlpha (0.5f));
        for (float y = cheek.getY() + 9; y < cheek.getBottom(); y += 23)
            g.drawLine (cheek.getX() + 4, y, cheek.getRight() - 4, y + 9, 1.0f);
    }

    g.setGradientFill (juce::ColourGradient (colours::panelLight, face.getX(), face.getY(), colours::panel, face.getX(), face.getBottom(), false));
    g.fillRect (face);

    g.setColour (colours::rule);
    for (const auto& r : layout.rules)
        if (! (r.x0 < 100.0f && r.y > 200.0f && r.y < 300.0f)) // Animate's rule: it now uses Master's, at the top
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.x0, r.y - 3.0f, r.x1, r.y + 3.0f));

    // Logos: "novation" as type, with the help button where the square mark is; PEAK traced
    // from the panel.
    g.setColour (colours::text);
    g.setFont (font (44.0f, true));
    g.drawText ("novation", juce::Rectangle<float> (face.getX() + 62, face.getY() + 6, 300, 52), juce::Justification::centredLeft);
    {
        const auto help = helpButtonArea();
        g.drawEllipse (help.reduced (1.5f), 3.0f);
        g.setFont (font (26.0f, true));
        g.drawText ("?", help.translated (0.0f, 1.0f), juce::Justification::centred);
    }
    g.fillPath (peakLogo (face.getRight() - 22.0f, face.getY() + 21.0f, 35.0f)); // where it sits in the photo

    for (const auto& c : controls)
    {
        const auto& e = *c.e;
        if (e.type == PanelElement::text)
        {
            g.setColour (colours::text);
            g.setFont (font (e.w, e.flag));
            const auto just = std::strcmp (e.style, "centre") == 0 ? juce::Justification::centred
                            : std::strcmp (e.style, "right") == 0  ? juce::Justification::centredRight
                                                                     : juce::Justification::centredLeft;
            const auto width = 700.0f;
            const auto x = just == juce::Justification::centred ? e.x - width / 2 : just == juce::Justification::centredRight ? e.x - width : e.x;
            g.drawText (utf8 (e.label), juce::Rectangle<float> (x, e.y - e.w, width, e.w * 2), just);
        }
        else if (e.type == PanelElement::arrow)
        {
            juce::Path tri;
            tri.addTriangle (e.x - 6, e.y - 7, e.x - 6, e.y + 7, e.x + 7, e.y);
            g.setColour (colours::rule);
            g.fillPath (tri);
        }
        else if (e.type == PanelElement::frame)
        {
            // Printed outlines: a box open at the top (VCA), or a "[" joining a control to its selector.
            juce::Path p;
            if (std::strcmp (e.style, "left") == 0)
            {
                p.startNewSubPath (e.x1, e.y);
                p.lineTo (e.x, e.y);
                p.lineTo (e.x, e.y1);
                p.lineTo (e.x1, e.y1);
            }
            else
            {
                p.startNewSubPath (e.x, e.y);
                p.lineTo (e.x, e.y1);
                p.lineTo (e.x1, e.y1);
                p.lineTo (e.x1, e.y);
            }
            g.setColour (std::strcmp (e.style, "left") == 0 ? colours::text.withAlpha (0.85f) : juce::Colour (0xff4b4c50));
            g.strokePath (p, juce::PathStrokeType (std::strcmp (e.style, "left") == 0 ? 1.6f : 2.5f));
        }
        else if (e.type == PanelElement::bracket)
        {
            // Printed bracket: a line with upturned ends; a label sits in a gap in the middle.
            g.setColour (colours::text.withAlpha (0.85f));
            const auto mid = (e.x + e.x1) / 2;
            const auto gap = *e.label != 0 ? 24.0f : 0.0f;
            g.drawLine (e.x, e.y, mid - gap, e.y, 1.6f);
            g.drawLine (mid + gap, e.y, e.x1, e.y, 1.6f);
            g.drawLine (e.x, e.y, e.x, e.y - 7, 1.6f);
            g.drawLine (e.x1, e.y, e.x1, e.y - 7, 1.6f);
            if (gap > 0)
            {
                g.setFont (font (13.0f));
                g.drawText (utf8 (e.label), juce::Rectangle<float> (mid - 40, e.y - 9, 80, 18), juce::Justification::centred);
            }
        }
    }
}

void FrontPanel::drawKnob (juce::Graphics& g, const Control& c) const
{
    const auto& e = *c.e;
    const auto s = e.flag ? bigScale : 1.0f;
    const juce::Point<float> centre (e.x, e.y);

    // Printed scale: 11 dots over 300 degrees with a tick at 12 o'clock.
    g.setColour (colours::text.withAlpha (0.9f));
    for (int i = 0; i <= 10; ++i)
    {
        const auto a = -sweep + (float) i * sweep / 5.0f;
        const auto p = centre.getPointOnCircumference (knobDotRadius * s, a);
        if (i == 5)
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (knobDotRadius * s - 4, a), centre.getPointOnCircumference (knobDotRadius * s + 5, a)), 3.0f);
        else
            g.fillEllipse (juce::Rectangle<float> (4.4f, 4.4f).withCentre (p));
    }

    // Ribbed black skirt.
    const auto skirt = juce::Rectangle<float> (knobSkirt * 2 * s, knobSkirt * 2 * s).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), e.x, e.y - knobSkirt * s, juce::Colour (0xff0b0b0b), e.x, e.y + knobSkirt * s, false));
    g.fillEllipse (skirt);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    for (int i = 0; i < 28; ++i)
    {
        const auto a = (float) i * juce::MathConstants<float>::twoPi / 28.0f;
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference ((knobCap + 4) * s, a), centre.getPointOnCircumference (knobSkirt * s - 1, a)), 1.4f);
    }

    // Brushed aluminium cap with pointer.
    const auto capR = knobCap * s;
    const auto cap = juce::Rectangle<float> (capR * 2, capR * 2).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f7f7), e.x - capR * 0.6f, e.y - capR, juce::Colour (0xffb9b9b9), e.x + capR, e.y + capR, false));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (cap, 1.0f);

    const auto* p = resolve (c);
    auto a = -sweep + 2.0f * sweep * (p != nullptr ? normalised (*p, model.get (*p)) : 0.5f);
    g.setColour (juce::Colour (0xff111111));
    g.drawLine (juce::Line<float> (centre.getPointOnCircumference (capR * 0.15f, a), centre.getPointOnCircumference (capR * 0.92f, a)), 3.2f * s);

    if (*e.label != 0)
    {
        g.setColour (colours::text);
        g.setFont (font (13.5f));
        const auto y = e.y + knobDotRadius * s + 4;
        g.drawText (utf8 (e.label), juce::Rectangle<float> (e.x - 80, y, 160, 18), juce::Justification::centred);
    }
}

void FrontPanel::drawButton (juce::Graphics& g, const Control& c) const
{
    const auto& e = *c.e;
    const auto r = juce::Rectangle<float> (e.w, e.h).withCentre ({ e.x, e.y });
    const juce::String style (e.style);

    bool on = false;
    if (c.menu.isNotEmpty())
        on = c.menu == openMenuName;
    else if (c.action == "compare")
        on = model.isComparing();
    else if (std::strcmp (e.id, "anim_hold") == 0)
        on = animateHold;
    else if (! c.cycle && ! c.alternatives.empty())
        if (const auto* p = resolve (c))
            on = model.get (*p) != 0;
    if (style == "lit")
        on = true;

    if (on)
    {
        g.setColour (colours::litEdge.withAlpha (0.35f));
        g.fillRoundedRectangle (r.expanded (5), 6.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, r.getCentreX(), r.getCentreY(), colours::lit, r.getX(), r.getY(), true));
    }
    else if (style == "white")
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f4f4), r.getX(), r.getY(), juce::Colour (0xffcfcfcf), r.getX(), r.getBottom(), false));
    else if (style == "animate")
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9d9b0), r.getX(), r.getY(), juce::Colour (0xffc7b080), r.getX(), r.getBottom(), false));
    else if (style == "hold")
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9773a), r.getX(), r.getY(), juce::Colour (0xff8a4d22), r.getX(), r.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2a), r.getX(), r.getY(), juce::Colour (0xff0e0e0e), r.getX(), r.getBottom(), false));

    g.fillRoundedRectangle (r, 3.5f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (r, 3.5f, 1.5f);

    if (*e.label != 0)
    {
        const auto inside = style == "animate" || style == "hold";
        g.setColour (inside ? juce::Colour (0xff7a5a2a) : colours::text);
        g.setFont (font (inside ? (style == "hold" ? 11.0f : 18.0f) : 13.5f));
        if (inside)
            g.drawText (utf8 (e.label), r, juce::Justification::centred);
        else
            g.drawText (utf8 (e.label), juce::Rectangle<float> (e.x - 70, r.getBottom() + 1, 140, 18), juce::Justification::centred);
    }
}

void FrontPanel::drawLed (juce::Graphics& g, const Control& c) const
{
    const auto& e = *c.e;
    const auto r = juce::Rectangle<float> (17.0f, 7.5f).withCentre ({ e.x, e.y });
    const auto on = c.source.valid() && read (c.source) == e.value;

    if (on)
    {
        g.setColour (colours::ledOn.withAlpha (0.35f));
        g.fillRoundedRectangle (r.expanded (4, 3), 5.0f);
        g.setColour (colours::ledOn);
    }
    else
    {
        g.setColour (colours::ledOff);
    }
    g.fillRoundedRectangle (r, 3.7f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (r, 3.7f, 1.0f);

    if (*e.label == '~')
    {
        g.setColour (colours::text);
        drawWaveIcon (g, utf8 (e.label + 1), juce::Rectangle<float> (e.x + 18, e.y - 5, 15, 10));
    }
    else if (*e.label != 0)
    {
        g.setColour (colours::text);
        g.setFont (font (12.5f));
        g.drawText (utf8 (e.label), juce::Rectangle<float> (e.x + 18, e.y - 9, 90, 18), juce::Justification::centredLeft);
    }
}

void FrontPanel::drawFader (juce::Graphics& g, const Control& c) const
{
    const auto& e = *c.e;
    g.setColour (colours::text.withAlpha (0.9f));
    for (int i = 0; i <= 10; ++i)
    {
        const auto y = e.y + (e.y1 - e.y) * (float) i / 10.0f;
        const auto w = (i == 0 || i == 10) ? 3.0f : 2.0f;
        g.drawLine (e.x - 34, y, e.x - 13, y, w);
        g.drawLine (e.x + 13, y, e.x + 34, y, w);
    }
    g.setColour (juce::Colour (0xff050505));
    g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (e.x - 8, e.y - 6, e.x + 8, e.y1 + 6), 3.0f);

    const auto* p = resolve (c);
    const auto norm = p != nullptr ? normalised (*p, model.get (*p)) : 0.0f;
    const auto y = e.y1 - norm * (e.y1 - e.y);
    const auto cap = juce::Rectangle<float> (50, 32).withCentre ({ e.x, y });
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3b3b), cap.getX(), cap.getY(), juce::Colour (0xff101010), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (colours::text);
    g.drawLine (cap.getX() + 4, y, cap.getRight() - 4, y, 2.2f);

    g.setFont (font (13.5f));
    g.drawText (utf8 (e.label), juce::Rectangle<float> (e.x - 60, e.y1 + 6, 120, 18), juce::Justification::centred);
}

void FrontPanel::drawDisplay (juce::Graphics& g, const PanelElement& e) const
{
    const auto r = juce::Rectangle<float>::leftTopRightBottom (e.x, e.y, e.x1, e.y1);
    g.setColour (juce::Colour (0xff090a0c));
    g.fillRoundedRectangle (r, 4.0f);

    const auto lineH = r.getHeight() / 4.0f;
    const auto inner = r.reduced (14.0f, 10.0f);
    const auto mono = [] (float h) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)); };
    // Text as large as `height`, shrunk to fit `width` (monospaced: about 0.6 em per character).
    const auto fitted = [&] (const juce::String& text, float height, float width) {
        return mono (juce::jmin (height, width / juce::jmax (1, text.length()) / 0.6f));
    };
    const auto smallH = r.getHeight() * 0.13f;

    if (message.isNotEmpty())
    {
        const auto textH = juce::jmin (lineH * 0.98f, (r.getWidth() - 16.0f) / 22.0f / 0.6f);
        g.setColour (colours::oled);
        g.setFont (mono (textH));
        for (int i = 0; i < 3; ++i)
            g.drawText (i == 0 ? juce::String ("INFO") : message.substring ((i - 1) * 22, i * 22),
                        juce::Rectangle<float> (r.getX() + 8, r.getY() + 2 + lineH * (float) i, r.getWidth() - 16, lineH), juce::Justification::centredLeft);
        return;
    }

    const auto paramAlpha = parameterAlpha();

    // The patch: number top left, category top right, the name large in the middle.
    if (paramAlpha < 1.0f)
    {
        g.setColour (colours::oled.withMultipliedAlpha (1.0f - paramAlpha));
        g.setFont (mono (smallH));
        const auto top = inner.withHeight (smallH * 1.2f);
        // Where the patch is: its Peak slot, or (loaded from My Banks) the bank's name and position,
        // cut short to stay clear of the category.
        const auto place = currentPlace (model);
        if (place.bank >= 4)
        {
            // As much of the name as fits, leaving a two-character gap before the category.
            auto bankName = model.getLibrary().getBanks()[place.bank - 4];
            const auto& categoryParam = *peak::findParam ("patch_category");
            const auto category = juce::String (peak::formatValue (categoryParam, model.get (categoryParam)));
            const auto charW = smallH * 0.6f;
            const auto maxChars = juce::jmax (4, (int) (top.getWidth() / charW) - category.length() - 2);
            if (bankName.length() > maxChars)
                bankName = bankName.substring (0, maxChars - 1).trimEnd() + juce::String::fromUTF8 ("\xe2\x80\xa6");
            g.drawText (bankName, top, juce::Justification::centredLeft);
        }
        else if (const auto where = model.getOriginalLocation())
            g.drawText (juce::String (where->name()), top, juce::Justification::centredLeft);
        if (! model.hasPatch())
        {
            g.setFont (fitted ("Backup page > Go", r.getHeight() * 0.16f, inner.getWidth()));
            g.drawText ("No patch loaded", inner.withTrimmedBottom (inner.getHeight() * 0.25f), juce::Justification::centred);
            g.drawText ("Backup page > Go", inner.withTrimmedTop (inner.getHeight() * 0.35f), juce::Justification::centred);
        }
        else
        {
            const auto& category = *peak::findParam ("patch_category");
            const auto categoryText = juce::String (peak::formatValue (category, model.get (category)));
            g.drawText (categoryText, top, juce::Justification::centredRight);
            // Click the category to change it (saved at once).
            const auto categoryW = juce::GlyphArrangement::getStringWidth (mono (smallH), categoryText);
            categoryArea = top.withLeft (top.getRight() - juce::jmax (categoryW, smallH * 2.0f) - 6.0f);

            // The name, large and centred, with its favourite star; "Edited" below it in amber.
            const auto edited = model.isModified() && ! model.isComparing();
            const auto fade = 1.0f - paramAlpha;
            const auto name = juce::String (model.getPatch().name()).trim();
            const auto starW = r.getHeight() * 0.16f;
            const auto nameFont = fitted (name, r.getHeight() * 0.30f, inner.getWidth() - 2.0f * (starW + 10.0f));
            const auto nameW = juce::GlyphArrangement::getStringWidth (nameFont, name);
            nameArea = inner.withSizeKeepingCentre (nameW, nameFont.getHeight() * 1.3f);
            g.setColour (colours::oled.withMultipliedAlpha (fade));
            g.setFont (nameFont);
            g.drawText (name, nameArea, juce::Justification::centred);

            const auto home = DrawerStyle::loadedPatchKey (model);
            const auto& library = model.getLibrary();
            const auto* entry = home.startsWith ("l:") ? library.find (home.substring (2)) : nullptr;
            const bool favourite = entry != nullptr ? entry->favourite : library.isPeakFavourite (model.getReference());
            starArea = juce::Rectangle<float> (starW * 1.4f, nameArea.getHeight()).withRightX (nameArea.getX() - 6.0f).withY (nameArea.getY());
            g.setColour ((favourite ? DrawerStyle::star : colours::oled.withAlpha (0.35f)).withMultipliedAlpha (fade));
            g.setFont (juce::FontOptions (starW * 1.25f));
            g.drawText (juce::String::fromUTF8 (favourite ? "\xe2\x98\x85" : "\xe2\x98\x86"), starArea, juce::Justification::centred);

            if (model.isComparing() || edited)
            {
                g.setColour ((edited ? DrawerStyle::accent : colours::oled).withMultipliedAlpha (fade));
                g.setFont (mono (smallH));
                g.drawText (model.isComparing() ? "Comparing" : "Edited", inner.withTrimmedTop (inner.getHeight() - smallH * 1.2f), juce::Justification::centred);
            }
        }
    }

    // An envelope or filter control: its graph, with its name and value on one line below
    // (as in the OB-6 and TEO-5 editors).
    if (paramAlpha > 0.0f && drawGraph (g, inner, smallH, paramAlpha))
        return;

    // A touched parameter: its section, its name, and its value with the saved value in brackets.
    if (paramAlpha > 0.0f)
    {
        const auto& p = *shownParam;
        g.setColour (colours::oled.withMultipliedAlpha (paramAlpha));
        g.setFont (mono (smallH));
        g.drawText (juce::String (p.section).toUpperCase(), inner.withHeight (smallH * 1.2f), juce::Justification::centredLeft);

        const auto paramNameArea = inner.withTrimmedTop (inner.getHeight() * 0.18f).withHeight (inner.getHeight() * 0.30f);
        g.setFont (fitted (p.name, r.getHeight() * 0.19f, inner.getWidth()));
        g.drawText (p.name, paramNameArea, juce::Justification::centred);

        const auto value = juce::String (peak::formatValue (p, model.get (p)));
        // No saved value for a selector that only picks which depth the panel knob shows (Amp Env / Mod Env 1):
        // which one is stored makes no difference to the sound.
        const auto showSaved = p.isStored() && std::strcmp (p.id, "filter_env_select") != 0;
        const auto saved = showSaved ? "  (" + juce::String (peak::formatValue (p, model.getStored (p))) + ")" : juce::String();
        const auto valueArea = inner.withTrimmedTop (inner.getHeight() * 0.50f);
        auto big = fitted (value + saved, r.getHeight() * 0.30f, inner.getWidth());
        auto small = mono (big.getHeight() * 0.6f);
        const auto valueW = juce::GlyphArrangement::getStringWidth (big, value);
        const auto savedW = juce::GlyphArrangement::getStringWidth (small, saved);
        auto line = valueArea.withSizeKeepingCentre (valueW + savedW, valueArea.getHeight());
        g.setFont (big);
        g.drawText (value, line.removeFromLeft (valueW), juce::Justification::centredLeft);
        g.setFont (small);
        g.drawText (saved, line, juce::Justification::centredLeft);
    }
}
