#include "FrontPanel.h"

#include "peak/core/ParamFormat.h"
#include "ui/ParamRows.h"
#include "ui/DrawerStyle.h"
#include "ui/SlotWriter.h"

#include <cstring>

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
    if (menu == "Arp/Clock") return { "Arp" };
    if (menu == "Mod") return { "Mod Matrix" };
    if (menu == "Voice") return { "Voice", "Glide", "Mixer" };
    if (menu == "FX") return { "Effects", "Chorus", "Delay", "Reverb" }; // the FX matrix is on the Mod Matrix tab
    if (menu == "Filter") return { "Filter" };
    if (menu == "Patch") return { "Patch" };
    return {};
}

// Runs a callback when the menu call-out closes, then deletes itself.
struct MenuCloseWatcher final : juce::ComponentListener
{
    explicit MenuCloseWatcher (std::function<void()> f) : onClose (std::move (f)) {}

    void componentBeingDeleted (juce::Component& c) override
    {
        c.removeComponentListener (this);
        onClose();
        delete this;
    }

    std::function<void()> onClose;
};

// A menu page shown in a call-out: the section's parameters as sliders.
class MenuPage final : public juce::Component,
                       private juce::ChangeListener
{
public:
    MenuPage (PeakModel& m, const juce::String& title, std::vector<const peak::ParamDef*> params)
        : model (m), rows (m, params)
    {
        heading.setText (title.toUpperCase(), juce::dontSendNotification);
        heading.setFont (font (16.0f, true));
        heading.setColour (juce::Label::textColourId, colours::rule);
        addAndMakeVisible (heading);
        viewport.setViewedComponent (&rows, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        rows.refresh();
        rows.layout (720, 360);
        setSize (740, juce::jmin (560, rows.getHeight() + 44));
        model.changed.addChangeListener (this);
    }

    ~MenuPage() override { model.changed.removeChangeListener (this); }

    void resized() override
    {
        auto r = getLocalBounds();
        heading.setBounds (r.removeFromTop (30).withTrimmedLeft (8));
        viewport.setBounds (r);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rows.refresh(); }

    PeakModel& model;
    juce::Label heading;
    ParamRows rows;
    juce::Viewport viewport;
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
    return std::make_unique<MenuPage> (model, name + " menu", params);
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
                                                      "data_encoder", "value_down", "value_up", "value_label", "menu_patch" };
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
        else if (std::strcmp (e.id, "patch_encoder") == 0)
        {
            auto& encoder = changedElements.emplace_back (e);
            encoder.y = 290.0f; // up to make room for Bank -/+ below Patch -/+
            c.e = &encoder;
        }
        else if (std::strcmp (e.id, "patch_down") == 0 || std::strcmp (e.id, "patch_up") == 0 || std::strcmp (e.id, "patch_pm_label") == 0)
        {
            auto& moved = changedElements.emplace_back (e);
            moved.y -= 63.0f;
            c.e = &moved;
        }
        else if (juce::String (e.bind).startsWith ("menu:") && e.type == PanelElement::button)
        {
            // The menu buttons line up with the screen's left edge.
            auto& moved = changedElements.emplace_back (e);
            moved.x -= 107.6f;
            c.e = &moved;
        }
        else if (std::strcmp (e.id, "audition") == 0)
        {
            auto& revert = changedElements.emplace_back (e);
            revert.label = "Revert";
            revert.bind = "action:revert";
            c.e = &revert;
        }
        const juce::String bind (e.bind);

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

    // Bank -/+ below Patch -/+, where Patch -/+ sat on the hardware.
    for (const auto& added : { PanelElement { PanelElement::button, "bank_down", 251.9f, 438.0f, 0.0f, 0.0f, 62.0f, 26.0f, "", "action:bank_down", 0, false, "dark" },
                               PanelElement { PanelElement::button, "bank_up", 336.3f, 438.1f, 0.0f, 0.0f, 62.0f, 26.0f, "", "action:bank_up", 0, false, "dark" },
                               PanelElement { PanelElement::text, "bank_pm_label", 290.7f, 471.4f, 0.0f, 0.0f, 12.0f, 0.0f, "–      Bank      +", "", 0, false, "centre" } })
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
        if (dragging->action == "patch_encoder")
        {
            dragStartY = ev.position.y;
            dragStartValue = model.getOriginalLocation().value_or (peak::Patch::Location {}).slot;
            patchStepsApplied = 0;
            return;
        }
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
    // Patch encoder: one patch per 3 px of vertical drag, from 000 to 127 of the current bank.
    if (dragging != nullptr && dragging->action == "patch_encoder")
    {
        const auto slot = juce::jlimit (0, 127, dragStartValue + juce::roundToInt ((dragStartY - ev.position.y) / 3.0f));
        if (slot != patchStepsApplied + dragStartValue)
        {
            selectSlotInBank (slot);
            patchStepsApplied = slot - dragStartValue;
        }
        return;
    }

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
    dragging = nullptr;
    dragParam = nullptr;
}

void FrontPanel::mouseDoubleClick (const juce::MouseEvent& ev)
{
    if (auto* c = controlAt (toDesign (ev.position)); c != nullptr && (c->e->type == PanelElement::knob || c->e->type == PanelElement::fader))
        if (const auto* p = resolve (*c))
            setParam (*p, DrawerStyle::initValue (*p));
}

void FrontPanel::mouseWheelMove (const juce::MouseEvent& ev, const juce::MouseWheelDetails& wheel)
{
    auto* c = controlAt (toDesign (ev.position));
    if (c == nullptr || (c->e->type != PanelElement::knob && c->e->type != PanelElement::fader))
        return;

    if (c->action == "patch_encoder")
    {
        const auto slot = model.getOriginalLocation().value_or (peak::Patch::Location {}).slot;
        selectSlotInBank (juce::jlimit (0, 127, slot + (wheel.deltaY > 0 ? 1 : -1)));
        return;
    }

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
            setParam (p, value);
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
    {
        // The same patch number in the next/previous bank (D wraps to A).
        const auto where = model.getOriginalLocation().value_or (peak::Patch::Location {});
        if (! model.selectPeakSlot ({ (where.bank + (c.action == "bank_up" ? 1 : 3)) % 4, where.slot }))
            showMessage ("No MIDI output to the Peak");
    }
    else if (c.action == "patch_up" || c.action == "patch_down")
    {
        if (! model.stepPeakSlot (c.action == "patch_up" ? 1 : -1))
            showMessage ("No MIDI output to the Peak");
    }
    else if (c.action == "initialise")
    {
        // As the Peak's own Initialise: the Init Patch becomes the current sound (nothing saved).
        if (! model.audition (DrawerStyle::emptyPatch(), "Init Patch (not saved)"))
            showMessage ("No MIDI output to the Peak");
    }
    else if (c.action.isNotEmpty())
        showMessage ("Not available yet");
}

void FrontPanel::selectSlotInBank (int slot)
{
    const auto where = model.getOriginalLocation().value_or (peak::Patch::Location {});
    if (slot != where.slot && ! model.selectPeakSlot ({ where.bank, slot }))
        showMessage ("No MIDI output to the Peak");
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

    const auto area = juce::Rectangle<float> (from.w + 8, from.h + 8).withCentre ({ from.x, from.y }).transformedBy (designToScreen()).toNearestInt();
    auto& box = juce::CallOutBox::launchAsynchronously (std::move (page), localAreaToGlobal (area), nullptr);
    box.setArrowSize (12.0f);
    box.addComponentListener (new MenuCloseWatcher ([safe = juce::Component::SafePointer<FrontPanel> (this)] {
        if (safe != nullptr)
        {
            safe->openMenuName.clear();
            safe->repaint();
        }
    }));
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
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.x0, r.y - 3.0f, r.x1, r.y + 3.0f));

    // Logos (drawn as type; the real artwork is not reproduced).
    g.setColour (colours::text);
    g.setFont (font (44.0f, true));
    g.drawText ("novation", juce::Rectangle<float> (face.getX() + 62, face.getY() + 6, 300, 52), juce::Justification::centredLeft);
    g.drawRoundedRectangle (juce::Rectangle<float> (face.getX() + 18, face.getY() + 14, 36, 36), 6.0f, 3.0f);
    g.setFont (font (50.0f, true).withExtraKerningFactor (0.08f));
    g.drawText ("PEAK", juce::Rectangle<float> (face.getRight() - 260, face.getY() + 6, 252, 56), juce::Justification::centredRight);

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
    if (c.action == "patch_encoder") // fully left = patch 000, fully right = 127, in the current bank
        a = -sweep + 2.0f * sweep * (float) model.getOriginalLocation().value_or (peak::Patch::Location {}).slot / 127.0f;
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
    // About 22 characters fit across, as on the Peak's screen.
    const auto textH = juce::jmin (lineH * 0.98f, (r.getWidth() - 16.0f) / 22.0f / 0.6f);
    auto line = [&] (int i, const juce::String& left, const juce::String& right, bool highlight) {
        const auto row = juce::Rectangle<float> (r.getX() + 8, r.getY() + 2 + lineH * (float) i, r.getWidth() - 16, lineH);
        if (highlight)
        {
            g.setColour (colours::oled.withAlpha (0.18f));
            g.fillRect (row);
        }
        g.setColour (colours::oled);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), textH, juce::Font::plain)));
        g.drawText (left, row, juce::Justification::centredLeft);
        g.drawText (right, row, juce::Justification::centredRight);
    };

    if (message.isNotEmpty())
    {
        line (0, "INFO", {}, false);
        line (1, message.substring (0, 22), {}, false);
        line (2, message.substring (22, 44), {}, false);
        return;
    }

    const auto* p = model.getLastTouched();
    if (p == nullptr)
    {
        // The current patch: slot, name, category, and whether it has been edited.
        const auto where = model.getOriginalLocation();
        line (0, "PATCH", where ? juce::String (where->name()) : juce::String(), false);
        if (! model.hasPatch())
        {
            line (1, "No patch loaded", {}, false);
            line (2, "Backup page > Go", {}, false);
            return;
        }
        const auto& category = *peak::findParam ("patch_category");
        line (1, juce::String (model.getPatch().name()), {}, false);
        line (2, "Category", juce::String (peak::formatValue (category, model.get (category))), false);
        line (3, model.isComparing() ? "Comparing" : model.isModified() ? "Edited" : "As saved", {}, false);
        return;
    }

    // Like the Peak: section title, then three parameters with the touched one highlighted.
    const auto& all = peak::allParams();
    int first = peak::indexOf (*p);
    while (first > 0 && std::strcmp (all[(size_t) first - 1].section, p->section) == 0 && peak::indexOf (*p) - first < 1)
        --first;

    line (0, juce::String (p->section).toUpperCase(), {}, false);
    for (int i = 0; i < 3; ++i)
    {
        const auto idx = first + i;
        if (idx >= (int) all.size() || std::strcmp (all[(size_t) idx].section, p->section) != 0)
            break;
        const auto& q = all[(size_t) idx];
        line (i + 1, q.name, peak::formatValue (q, model.get (q)), &q == p);
    }
}
