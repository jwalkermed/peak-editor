#include "CardStyle.h"

#include "ui/DrawerStyle.h"

namespace CardStyle
{
Look::Look()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::Label::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, card);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cellHover);
    setColour (juce::PopupMenu::highlightedTextColourId, amber);
    setColour (juce::ScrollBar::thumbColourId, barFill);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, amber);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, barFill);
    setColour (juce::CaretComponent::caretColourId, amber);
    setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff141517));
}

void Look::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h, bool ticked, bool enabled, bool over, bool)
{
    const auto r = juce::Rectangle<float> (x, y, w, h).reduced (1.0f);
    g.setColour (over ? cellHover : cell);
    g.fillRoundedRectangle (r, 3.5f);
    g.setColour (ticked ? amber : line.brighter (0.3f));
    g.drawRoundedRectangle (r, 3.5f, 1.0f);
    if (ticked)
    {
        juce::Path tick;
        tick.startNewSubPath (r.getX() + r.getWidth() * 0.25f, r.getCentreY());
        tick.lineTo (r.getX() + r.getWidth() * 0.43f, r.getBottom() - r.getHeight() * 0.27f);
        tick.lineTo (r.getRight() - r.getWidth() * 0.22f, r.getY() + r.getHeight() * 0.27f);
        g.setColour (amber.withAlpha (enabled ? 1.0f : 0.4f));
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void Look::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (cell);
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f), 5.0f);
}

void Look::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) && ! e.isReadOnly() ? blue : line);
    g.drawRoundedRectangle (juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f), 5.0f, 1.0f);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style,
                             juce::Slider& slider)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        // A line with a round handle (like the keyboard's velocity), coloured from the centre (or the
        // minimum) to the handle.
        const auto cy = (float) y + (float) h * 0.5f;
        const auto track = juce::Rectangle<float> ((float) x, cy - 1.5f, (float) w, 3.0f);
        g.setColour (line.brighter (0.15f));
        g.fillRoundedRectangle (track, 1.5f);
        const auto centre = slider.getProperties()["centre"];
        const auto from = centre.isVoid() ? (float) x : (float) slider.getPositionOfValue ((double) centre);
        g.setColour (blue);
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, pos), cy - 1.5f, juce::jmax (from, pos), cy + 1.5f), 1.5f);
        if (! centre.isVoid())
        {
            g.setColour (dim);
            g.fillRect (juce::Rectangle<float> (1.0f, 9.0f).withCentre ({ from, cy }));
        }
        const auto knob = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ pos, cy });
        g.setColour (slider.isMouseOverOrDragging() ? juce::Colours::white : text);
        g.fillEllipse (knob);
        g.setColour (blue);
        g.drawEllipse (knob.reduced (0.5f), 1.0f);
        return;
    }
    if (style != juce::Slider::LinearBar)
        return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, slider);

    const auto box = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.5f, 1.5f);
    g.setColour (cell);
    g.fillRoundedRectangle (box, 5.0f);

    // From the minimum, or from the centre for values shown as -/+.
    const auto centre = slider.getProperties()["centre"];
    const auto from = centre.isVoid() ? box.getX() + 2.0f : (float) slider.getPositionOfValue ((double) centre);
    const auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, pos), box.getY() + 2.0f, juce::jmax (from, pos), box.getBottom() - 2.0f);
    if (fill.getWidth() > 1.0f)
    {
        g.setColour (barFill);
        g.fillRoundedRectangle (fill, 4.0f);
    }
    if (! centre.isVoid())
    {
        g.setColour (dim);
        g.fillRect (juce::Rectangle<float> (1.0f, box.getHeight() - 8.0f).withCentre ({ from, box.getCentreY() }));
    }
    g.setColour (slider.isMouseOverOrDragging() ? dim : line);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);

    // An optional name inside the bar, at the left ("label" in the slider's properties).
    if (const auto label = slider.getProperties()["label"].toString(); label.isNotEmpty())
    {
        g.setColour (text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (label, box.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
    }
}

juce::Label* Look::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setJustificationType (juce::Justification::centredRight);
    l->setBorderSize ({ 1, 4, 1, 10 });
    l->setFont (juce::FontOptions (13.0f));
    return l;
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f, 1.5f);
    g.setColour (box.isMouseOver (true) ? cellHover : cell);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path arrow;
    const auto c = juce::Point<float> (r.getRight() - 14.0f, r.getCentreY());
    arrow.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
    g.setColour (dim);
    g.fillPath (arrow);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font Look::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (13.0f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = b.getProperties()["primary"];
    g.setColour (primary ? closeFill.brighter (over ? 0.15f : 0.0f) : (over || down ? cellHover : cell));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (primary ? amber : line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int)
{
    return juce::FontOptions (14.0f);
}

Look& look()
{
    static Look instance;
    return instance;
}

void paintCard (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title, const juce::String& note)
{
    const auto b = bounds.toFloat().reduced (0.5f);
    g.setColour (card);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (border);
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    auto top = bounds.reduced (18, 0).withHeight (52);
    g.setColour (blue);
    g.setFont (headingFont (15.0f));
    g.drawText (title, top, juce::Justification::centredLeft);
    if (note.isNotEmpty())
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (note, top, juce::Justification::centredRight);
    }
}

juce::Font headingFont (float height)
{
    return DrawerStyle::font (height);
}

void paintTabHeader (juce::Graphics& g, juce::Rectangle<int> tab, const juce::String& title, int titleX)
{
    g.fillAll (DrawerStyle::background);
    g.setColour (DrawerStyle::silkscreen);
    g.setFont (headingFont (14.0f));
    g.drawText (title.toUpperCase(), juce::Rectangle<int> (tab.getX() + titleX + 5, tab.getY() + 8, 300, 24), juce::Justification::centredLeft);
    g.setColour (DrawerStyle::pinstripe);
    g.fillRect (tab.getX() + 12, tab.getY() + 34, tab.getWidth() - 24, 1);
}

void styleHeading (juce::Label& l)
{
    l.setFont (headingFont (11.0f));
    l.setColour (juce::Label::textColourId, blue);
    l.setBorderSize ({ 0, 0, 0, 0 });
}

//==============================================================================
Overlay::Overlay (std::unique_ptr<juce::Component> c, std::function<void()> closed) : card (std::move (c)), onClosed (std::move (closed))
{
    preferredSize = { card->getWidth(), card->getHeight() };
    addAndMakeVisible (*card);
    setWantsKeyboardFocus (true);
}

Overlay::~Overlay() = default;

void Overlay::close()
{
    if (std::exchange (closing, true))
        return;
    // Later, so a button inside the card can call this from its own click.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Overlay> (this)] {
        if (safe != nullptr && safe->onClosed)
            safe->onClosed(); // the owner deletes the overlay
    });
}

void Overlay::resized()
{
    const auto area = getLocalBounds().reduced (12);
    card->setBounds (juce::Rectangle<int> (juce::jmin (preferredSize.x, area.getWidth()), juce::jmin (preferredSize.y, area.getHeight()))
                         .withCentre (area.getCentre()));
}

void Overlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.35f)); // the panel stays visible behind
}

void Overlay::mouseDown (const juce::MouseEvent& e)
{
    if (! card->getBounds().contains (e.getPosition()))
        close();
}

bool Overlay::keyPressed (const juce::KeyPress& key)
{
    if (card->keyPressed (key)) // e.g. the wavetable picker's arrow keys
        return true;
    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (onEscape)
            onEscape();
        else
            close();
        return true;
    }
    return false;
}

//==============================================================================
HelpCard::HelpCard (const juce::String& t, std::vector<Section> sections, int width) : title (t)
{
    float y = 0.0f;
    const auto add = [&] (bool bullet, const juce::String& words, juce::Font font, juce::Colour colour, float spaceBefore) {
        juce::AttributedString text (words);
        text.setFont (font);
        text.setColour (colour);
        text.setLineSpacing (2.0f);
        Line l { bullet, y + spaceBefore, {} };
        l.layout.createLayout (text, (float) (width - 2 * pad - (bullet ? indent : 0)));
        y = l.y + l.layout.getHeight() + 3.0f;
        lines.push_back (std::move (l));
    };
    for (size_t i = 0; i < sections.size(); ++i)
    {
        add (false, sections[i].heading.toUpperCase(), headingFont (12.0f), blue, i > 0 ? 14.0f : 0.0f);
        for (const auto& point : sections[i].points)
            add (true, point, juce::Font (juce::FontOptions (14.0f)), CardStyle::text, 0.0f);
    }
    setSize (width, top + 14 + (int) std::ceil (y) + bottom);

    closeButton.getProperties().set ("primary", true);
    closeButton.setLookAndFeel (&look());
    closeButton.onClick = [this] {
        if (auto* overlay = findParentComponentOfClass<Overlay>())
            overlay->close();
    };
    addAndMakeVisible (closeButton);
}

void HelpCard::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds(), title);
    g.setColour (line);
    g.fillRect (pad, top, getWidth() - 2 * pad, 1);
    const auto origin = juce::Point<float> ((float) pad, (float) top + 14.0f);
    for (const auto& l : lines)
    {
        const auto x = origin.x + (l.bullet ? (float) indent : 0.0f);
        if (l.bullet)
        {
            g.setColour (amber);
            g.fillEllipse (origin.x + 2.0f, origin.y + l.y + 7.0f, 5.0f, 5.0f);
        }
        l.layout.draw (g, { x, origin.y + l.y, l.layout.getWidth(), l.layout.getHeight() });
    }
}

void HelpCard::resized()
{
    closeButton.setBounds (getLocalBounds().removeFromBottom (bottom).removeFromRight (100 + pad).withTrimmedRight (pad).withSizeKeepingCentre (100, 30));
}
} // namespace CardStyle
